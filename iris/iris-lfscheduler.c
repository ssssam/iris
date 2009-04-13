/* iris-lfscheduler.c
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

#include "iris-lfscheduler.h"
#include "iris-queue.h"
#include "iris-rrobin.h"
#include "iris-scheduler.h"
#include "iris-scheduler-private.h"
#include "iris-scheduler-manager.h"
#include "iris-lfqueue.h"

/**
 * SECTION:iris-lfscheduler
 * @short_description: A lock-free scheduler
 *
 * #IrisLFScheduler is a lock-free scheduler implementation.  Don't be fooled,
 * lock-free is not always better for all work-loads.  For very generic work
 * loads that have work-items of various lifetimes, you probably want
 * #IrisLFScheduler.  However, if you know your work items are typically very
 * short lived, #IrisLFScheduler might be what you want.  It's very fast at
 * handling work items created from within scheduler threads.
 *
 * The downside, is that each thread has its own queue and incoming work
 * items are round-robined into those queues.  Meaning if one queue gets
 * much more work than the others that thread is responsible for completing
 * those work items itself.  So your trade-off is less-contention vs
 * thread responsibility for itself.  Some work-loads prefer this.
 */

struct _IrisLFSchedulerPrivate
{
	IrisRRobin  *rrobin;       /* Round robin of per-thread queues used
	                            * by threads for work-stealing.
	                            */

	gboolean     has_leader;   /* Is there a leader thread */
};

G_DEFINE_TYPE (IrisLFScheduler, iris_lfscheduler, IRIS_TYPE_SCHEDULER);

static void
iris_lfscheduler_queue_real_cb (gpointer data,
                                gpointer user_data)
{
	IrisQueue      *queue;
	IrisThreadWork *thread_work;

	g_return_if_fail (data != NULL);
	g_return_if_fail (user_data != NULL);

	queue = data;
	thread_work = user_data;

	iris_queue_push (queue, thread_work);
}

static void
iris_lfscheduler_queue_real (IrisScheduler  *scheduler,
                             IrisCallback    func,
                             gpointer        data,
                             GDestroyNotify  notify)
{
	IrisLFSchedulerPrivate *priv;
	IrisThreadWork         *thread_work;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (func != NULL);

	priv = IRIS_LFSCHEDULER (scheduler)->priv;

	thread_work = iris_thread_work_new (func, data);

	/* deliver to next round robin */
	iris_rrobin_apply (IRIS_LFSCHEDULER (scheduler)->priv->rrobin,
	                   iris_lfscheduler_queue_real_cb,
	                   thread_work);
}

static void
iris_lfscheduler_add_thread_real (IrisScheduler  *scheduler,
                                  IrisThread     *thread)
{
	IrisLFSchedulerPrivate *priv;
	gboolean                leader;
	IrisQueue              *queue;

	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	priv = IRIS_LFSCHEDULER (scheduler)->priv;

	queue = iris_lfqueue_new ();
	thread->user_data = queue;

	/* add the queue to the round robin */
	if (!iris_rrobin_append (priv->rrobin, queue))
		goto error;

	/* check if this thread is the leader */
	leader = g_atomic_int_compare_and_exchange (&priv->has_leader, FALSE, TRUE);

	/* tell the thread to watch this queue */
	iris_thread_manage (thread, queue, leader);

	return;

error:
	g_warning ("Scheduler at thread-maximum, cannot add another thread");
	iris_queue_unref (queue);
	thread->user_data = NULL;
}

static void
iris_lfscheduler_remove_thread_real (IrisScheduler *scheduler,
                                     IrisThread    *thread)
{
	IrisLFSchedulerPrivate *priv;
	IrisQueue              *queue;
	gpointer                thread_work;

	g_return_if_fail (IRIS_IS_LFSCHEDULER (scheduler));
	g_return_if_fail (thread != NULL);

	queue = thread->user_data;
	thread->user_data = NULL;
	g_return_if_fail (queue != NULL);

	priv = IRIS_LFSCHEDULER (scheduler)->priv;

	iris_rrobin_remove (priv->rrobin, queue);

	/* apply left over items to other queues */
	while ((thread_work = iris_queue_try_pop (queue)) != NULL) {
		iris_rrobin_apply (priv->rrobin,
		                   iris_lfscheduler_queue_real_cb,
		                   thread_work);
	}

	iris_queue_unref (queue);
}

static void
iris_lfscheduler_finalize (GObject *object)
{
	IrisLFSchedulerPrivate *priv;

	priv = IRIS_LFSCHEDULER (object)->priv;

	G_OBJECT_CLASS (iris_lfscheduler_parent_class)->finalize (object);
}

static void
iris_lfscheduler_class_init (IrisLFSchedulerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	IrisSchedulerClass *sched_class = IRIS_SCHEDULER_CLASS (klass);

	sched_class->queue = iris_lfscheduler_queue_real;
	sched_class->add_thread = iris_lfscheduler_add_thread_real;
	sched_class->remove_thread = iris_lfscheduler_remove_thread_real;
	object_class->finalize = iris_lfscheduler_finalize;

	g_type_class_add_private (object_class, sizeof (IrisLFSchedulerPrivate));
}

static void
iris_lfscheduler_init (IrisLFScheduler *scheduler)
{
	guint max_threads;

	scheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheduler,
	                                               IRIS_TYPE_LFSCHEDULER,
	                                               IrisLFSchedulerPrivate);

	scheduler->priv->has_leader = FALSE;

	/* FIXME: This is technically broken since it gets modified
	 *   after we call it.
	 */

	max_threads = iris_scheduler_get_max_threads (IRIS_SCHEDULER (scheduler));
	scheduler->priv->rrobin = iris_rrobin_new (max_threads);
}

IrisScheduler*
iris_lfscheduler_new (void)
{
	return g_object_new (IRIS_TYPE_LFSCHEDULER, NULL);
}

IrisScheduler*
iris_lfscheduler_new_full (guint min_threads,
                           guint max_threads)
{
	IrisScheduler *scheduler;

	scheduler = iris_lfscheduler_new ();

	/* FIXME: This is technically broken since it gets modified
	 *   after we create the rrobin.
	 */
	//scheduler->priv->min_threads = min_threads;
	//scheduler->priv->max_threads = max_threads;

	return scheduler;
}
