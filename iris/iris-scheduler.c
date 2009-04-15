/* iris-scheduler.c
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

#ifdef LINUX
#include <sys/sysinfo.h>
#endif

#include <stdlib.h>

#include "iris-queue.h"
#include "iris-rrobin.h"
#include "iris-scheduler.h"
#include "iris-scheduler-private.h"
#include "iris-scheduler-manager.h"

/**
 * SECTION:iris-scheduler
 * @short_description: A generic, extendable scheduler for work items
 *
 * #IrisScheduler is a base class used for managing the scheduling of
 * work items onto active threads.  The default scheduler is sufficient
 * for most purposes.  However, if you need custom scheduling with
 * different queuing decisions you can create your own.
 *
 * By default, a scheduler will be given "min-threads" threads during
 * startup.  If a "leader" thread, (typically the first thread added)
 * feels it is getting behind, it will ask the scheduler manager for
 * more help.  The scheduler manager will try to first repurpose
 * existing threads, or create new threads if no existing threads are
 * available.  Based on the speed of work performed by the scheduler,
 * the manager will try to appropriate a sufficient number of threads.
 */

G_DEFINE_TYPE (IrisScheduler, iris_scheduler, G_TYPE_OBJECT);

static void
iris_scheduler_queue_rrobin_cb (gpointer data,
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
iris_scheduler_queue_real (IrisScheduler  *scheduler,
                           IrisCallback    func,
                           gpointer        data,
                           GDestroyNotify  notify)
{
	IrisSchedulerPrivate *priv;
	IrisThreadWork       *thread_work;
	IrisThread           *thread;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (func != NULL);

	priv = scheduler->priv;

	thread = iris_thread_get ();
	thread_work = iris_thread_work_new (func, data);

	iris_rrobin_apply (priv->rrobin, iris_scheduler_queue_rrobin_cb, thread_work);
}

static guint
iris_scheduler_get_n_cpu (void)
{
#ifdef LINUX
	static gint n_cpu = 0;
	if (G_UNLIKELY (n_cpu == 0)) {
		if (g_getenv ("IRIS_SCHED_MAX") != NULL)
			n_cpu = atoi (g_getenv ("IRIS_SCHED_MAX"));
		if (n_cpu == 0)
			n_cpu = get_nprocs ();
	}
	return n_cpu;
#else
	return 1;
#endif
}

static gint
iris_scheduler_get_min_threads_real (IrisScheduler *scheduler)
{
	gint min_threads;
	min_threads = scheduler->priv->min_threads;
	return (min_threads > 0) ? min_threads : 1;
}

static gint
iris_scheduler_get_max_threads_real (IrisScheduler *scheduler)
{
	IrisSchedulerPrivate *priv = scheduler->priv;
	if (G_UNLIKELY (priv->max_threads == 0))
		priv->max_threads = MAX (2, iris_scheduler_get_n_cpu ());
	return priv->max_threads;
}

static void
iris_scheduler_add_thread_real (IrisScheduler  *scheduler,
                                IrisThread     *thread)
{
	IrisSchedulerPrivate *priv;
	gboolean              leader;
	IrisQueue            *queue;
	gint                  max_threads;

	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	priv = scheduler->priv;

	/* initialize round robin for queues */
	if (G_UNLIKELY (!priv->rrobin)) {
		/* we must be getting called from sched-manager-prepare,
		 * so no need to lock our mutex as its already locked.
		 */
		max_threads = iris_scheduler_get_max_threads (scheduler);
		priv->rrobin = iris_rrobin_new (max_threads);
	}

	/* create the threads queue for the round robin */
	queue = iris_queue_new ();
	thread->user_data = queue;

	/* add the item to the round robin */
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
iris_scheduler_remove_thread_real (IrisScheduler *scheduler,
                                   IrisThread    *thread)
{
	/* FIXME: Implement */
}

static void
iris_scheduler_finalize (GObject *object)
{
	IrisSchedulerPrivate *priv;

	priv = IRIS_SCHEDULER (object)->priv;

	g_mutex_free (priv->mutex);

	G_OBJECT_CLASS (iris_scheduler_parent_class)->finalize (object);
}

static void
iris_scheduler_class_init (IrisSchedulerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->queue = iris_scheduler_queue_real;
	klass->get_min_threads = iris_scheduler_get_min_threads_real;
	klass->get_max_threads = iris_scheduler_get_max_threads_real;
	klass->add_thread = iris_scheduler_add_thread_real;
	klass->remove_thread = iris_scheduler_remove_thread_real;

	object_class->finalize = iris_scheduler_finalize;

	g_type_class_add_private (object_class, sizeof (IrisSchedulerPrivate));
}

static void
iris_scheduler_init (IrisScheduler *scheduler)
{
	scheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheduler,
	                                          IRIS_TYPE_SCHEDULER,
	                                          IrisSchedulerPrivate);
	scheduler->priv->min_threads = 0;
	scheduler->priv->max_threads = 0;
	scheduler->priv->mutex = g_mutex_new ();

	scheduler->priv->queue = g_async_queue_new ();
	g_assert (scheduler->priv->queue);
}

/**
 * iris_scheduler_new:
 *
 * Creates a new instance of #IrisScheduler with the defaults.
 *
 * Return value: the newly created #IrisScheduler instance.
 */
IrisScheduler*
iris_scheduler_new (void)
{
	return g_object_new (IRIS_TYPE_SCHEDULER, NULL);
}

/**
 * iris_scheduler_new_full:
 * @min_threads: The minimum number of threads to allocate
 * @max_threads: The maximum number of threads to allocate
 *
 * Creates a new scheduler with a defined set of thread ratios.
 *
 * Return value: the newly created scheduler instance.
 */
IrisScheduler*
iris_scheduler_new_full (guint min_threads,
                         guint max_threads)
{
	IrisScheduler *scheduler;

	scheduler = iris_scheduler_new ();
	scheduler->priv->min_threads = min_threads;
	scheduler->priv->max_threads = max_threads;

	return scheduler;
}

G_LOCK_DEFINE (default_scheduler);

/**
 * iris_scheduler_default:
 *
 * Retrieves the default scheduler which can be shared.
 *
 * Return value: a #IrisScheduler instance
 */
IrisScheduler*
iris_scheduler_default (void)
{
	static IrisScheduler *default_scheduler = NULL;

	if (G_UNLIKELY (default_scheduler == NULL)) {
		G_LOCK (default_scheduler);
		if (!g_atomic_pointer_get (&default_scheduler))
			default_scheduler = iris_scheduler_new ();
		G_UNLOCK (default_scheduler);
	}

	return default_scheduler;
}

/**
 * iris_scheduler_queue:
 * @scheduler: An #IrisScheduler
 * @func: An #IrisCallback
 * @data: data for @func
 * @notify: an optional callback after execution
 *
 * NOTE: notify will probably disappear soon
 *
 * Queues a new work item to be executed by one of the schedulers work
 * threads.
 */
void
iris_scheduler_queue (IrisScheduler  *scheduler,
                      IrisCallback    func,
                      gpointer        data,
                      GDestroyNotify  notify)
{
	IrisSchedulerPrivate *priv;

	g_return_if_fail (scheduler != NULL);

	priv = scheduler->priv;

	/* Lazy initialization of the scheduler. By holding off until we
	 * need this, we attempt to reduce our total thread usage.
	 */

	if (G_UNLIKELY (!priv->initialized)) {
		g_mutex_lock (priv->mutex);
		if (G_LIKELY (!g_atomic_int_get (&priv->initialized))) {
			iris_scheduler_manager_prepare (scheduler);
			g_atomic_int_set (&priv->initialized, TRUE);
		}
		g_mutex_unlock (priv->mutex);
	}

	IRIS_SCHEDULER_GET_CLASS (scheduler)->queue (scheduler, func, data, notify);
}

/**
 * iris_scheduler_get_max_threads:
 * @scheduler: An #IrisScheduler
 *
 * Retrieves the maximum number of threads the scheduler should be allocated.
 * The default is equal to the number of cpus unless there is only a single
 * cpu, in which case the default is 2.
 *
 * Currently, only Linux is supported for the number of cpus.  If you
 * would like another OS supported, please send an email with the method
 * to retreive the number of cpus (get_nprocs() on Linux).
 *
 * Return value: the maximum number of threads to allocate.
 */
gint
iris_scheduler_get_max_threads (IrisScheduler *scheduler)
{
	return IRIS_SCHEDULER_GET_CLASS (scheduler)->get_max_threads (scheduler);
}

/**
 * iris_scheduler_get_min_threads:
 * @scheduler: An #IrisScheduler
 *
 * Requests the minimum number of threads that the scheduler needs to
 * execute efficiently. This value should never change, and should always
 * be greater or equal to 1.
 *
 * Return value: the minimum number of threads to allocate to the scheduler.
 */
gint
iris_scheduler_get_min_threads (IrisScheduler *scheduler)
{
	return IRIS_SCHEDULER_GET_CLASS (scheduler)->get_min_threads (scheduler);
}

/**
 * iris_scheduler_add_thread:
 * @scheduler: An #IrisScheduler
 * @thread: An #IrisThread
 *
 * Requests that the scheduler add the thread to its set of executing
 * threads. It is the responsibility of the scheduler to tell the thread
 * to start managing a work queue with iris_thread_manage().
 */
void
iris_scheduler_add_thread (IrisScheduler *scheduler,
                           IrisThread    *thread)
{
	IRIS_SCHEDULER_GET_CLASS (scheduler)->add_thread (scheduler, thread);
	thread->scheduler = g_object_ref (scheduler);
}

/**
 * iris_scheduler_remove_thread:
 * @scheduler: An #IrisScheduler
 * @thread: An #IrisThread
 *
 * Requests that a scheduler remove the thread from current activity. If the
 * scheduler has a dedicated queue for the thread, it should flush the items
 * into another threads or set of threads queues.
 */
void
iris_scheduler_remove_thread (IrisScheduler *scheduler,
                              IrisThread    *thread)
{
	IRIS_SCHEDULER_GET_CLASS (scheduler)->remove_thread (scheduler, thread);

	/* We know that the threads scheduler definitely is no longer
	 * maxed out since this thread is ending.
	 */
	g_atomic_int_set (&thread->scheduler->maxed, FALSE);

	thread->scheduler = NULL;
	g_object_unref (scheduler);
}
