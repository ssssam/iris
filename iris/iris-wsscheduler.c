/* iris-wsscheduler.c
 *
 * Copyright (C) 2009 Christian Hergert <chris@dronelabs.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 
 * 02110-1301 USA
 */

#include "iris-queue.h"
#include "iris-rrobin.h"
#include "iris-scheduler.h"
#include "iris-scheduler-private.h"
#include "iris-scheduler-manager.h"
#include "iris-wsscheduler.h"
#include "iris-wsqueue.h"

/**
 * SECTION:iris-wsscheduler
 * @short_description: A work-stealing scheduler
 *
 * #IrisWSScheduler is a work-stealing scheduler implementation for iris.  It
 * uses an #IrisWSQueue per thread for storing work items yielded from the
 * the worker thread itself.  Since this can be done lock-less in most
 * situations, it should help when workloads create many recursive tasks.
 *
 * A global queue is used for work items generated from outside the schedulers
 * set of threads.
 */

struct _IrisWSSchedulerPrivate
{
	GMutex      *mutex;        /* Synchronization for setting up the
	                            * scheduler instance.  Provides for lazy
	                            * instantiation.
	                            */

	IrisRRobin  *rrobin;       /* Round robin of per-thread queues used
	                            * by threads for work-stealing.
	                            */

	IrisQueue   *queue;        /* Global Queue, used by work items
	                            * not originating from a thread within
	                            * the scheduler.
	                            */

	gboolean     has_leader;   /* Is there a leader thread */
};

G_DEFINE_TYPE (IrisWSScheduler, iris_wsscheduler, IRIS_TYPE_SCHEDULER);

static void
iris_wsscheduler_queue_real (IrisScheduler  *scheduler,
                             IrisCallback    func,
                             gpointer        data,
                             GDestroyNotify  notify)
{
	IrisWSSchedulerPrivate *priv;
	IrisThread             *thread;
	IrisThreadWork         *thread_work;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (func != NULL);

	priv = IRIS_WSSCHEDULER (scheduler)->priv;

	thread = iris_thread_get ();
	thread_work = iris_thread_work_new (func, data);

	/* If the current thread is an iris-thread and it is a member of our
	 * scheduler, then we will queue it to its own lock-free queue.  This
	 * helps keep cpu cache hits up as well since the local thread will already
	 * have the associated data hot.  However, we need to make sure the thread
	 * will take this item sooner so its own work doesn't invalidate cache.
	 */

	if (thread && thread->scheduler == scheduler && thread->active) {
		iris_wsqueue_local_push (IRIS_WSQUEUE (thread->active), thread_work);
		return;
	}

	iris_queue_push (priv->queue, thread_work);
}

static void
iris_wsscheduler_remove_thread_real (IrisScheduler *scheduler,
                                     IrisThread    *thread)
{
	/* FIXME: Implement */
}

static void
iris_wsscheduler_add_thread_real (IrisScheduler  *scheduler,
                                  IrisThread     *thread)
{
	IrisWSSchedulerPrivate *priv;
	gboolean                leader;
	IrisQueue              *queue;

	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	priv = IRIS_WSSCHEDULER (scheduler)->priv;

	/* create the threads queue for the round robin */
	queue = iris_wsqueue_new (priv->queue, priv->rrobin);
	thread->user_data = queue;

	/* add the queue to the round robin */
	if (!iris_rrobin_append (priv->rrobin, queue))
		goto error;

	/* check if this thread is the leader */
	leader = g_atomic_int_compare_and_exchange (&priv->has_leader, FALSE, TRUE);

	iris_thread_manage (thread, queue, leader);

	return;

error:
	iris_queue_unref (queue);
	thread->user_data = NULL;
}

static void
iris_wsscheduler_finalize (GObject *object)
{
	IrisWSSchedulerPrivate *priv;

	priv = IRIS_WSSCHEDULER (object)->priv;

	g_mutex_free (priv->mutex);

	G_OBJECT_CLASS (iris_wsscheduler_parent_class)->finalize (object);
}

static void
iris_wsscheduler_class_init (IrisWSSchedulerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	IrisSchedulerClass *sched_class = IRIS_SCHEDULER_CLASS (klass);

	sched_class->queue = iris_wsscheduler_queue_real;
	sched_class->add_thread = iris_wsscheduler_add_thread_real;
	sched_class->remove_thread = iris_wsscheduler_remove_thread_real;
	object_class->finalize = iris_wsscheduler_finalize;

	g_type_class_add_private (object_class, sizeof (IrisWSSchedulerPrivate));
}

static void
iris_wsscheduler_init (IrisWSScheduler *scheduler)
{
	guint max_threads;

	scheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheduler,
	                                               IRIS_TYPE_WSSCHEDULER,
	                                               IrisWSSchedulerPrivate);

	scheduler->priv->mutex = g_mutex_new ();
	scheduler->priv->queue = iris_queue_new ();
	scheduler->priv->has_leader = FALSE;

	/* FIXME: This is technically broken since it gets modified
	 *   after we call it.
	 */

	max_threads = iris_scheduler_get_max_threads (IRIS_SCHEDULER (scheduler));
	scheduler->priv->rrobin = iris_rrobin_new (max_threads);
}

IrisScheduler*
iris_wsscheduler_new (void)
{
	return g_object_new (IRIS_TYPE_WSSCHEDULER, NULL);
}

IrisScheduler*
iris_wsscheduler_new_full (guint min_threads,
                           guint max_threads)
{
	IrisScheduler *scheduler;

	scheduler = iris_wsscheduler_new ();
	scheduler->priv->min_threads = min_threads;
	scheduler->priv->max_threads = max_threads;

	return scheduler;
}
