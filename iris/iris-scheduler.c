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
#elif DARWIN
#include <sys/param.h>
#include <sys/sysctl.h>
#elif defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <stdlib.h>

#include "iris-debug.h"
#include "iris-queue.h"
#include "iris-rrobin.h"
#include "iris-scheduler.h"
#include "iris-scheduler-private.h"
#include "iris-scheduler-manager.h"

/**
 * SECTION:iris-scheduler
 * @short_description: A generic, extendable scheduler for work items
 * @see_also: #IrisGMainScheduler
 *
 * #IrisScheduler is a base class used for managing the scheduling of
 * work items onto active threads.  The standard scheduler is sufficient
 * for most purposes.  However, if you need custom scheduling with
 * different queuing decisions you can create your own.
 *
 * There are two default schedulers inside Iris. The control scheduler is
 * used for handling message processing, while the work scheduler is
 * suitable for tasks that will take much longer. This separation prevents slow
 * tasks from ever causing delays to important messages (the best example being
 * a slow-running task blocking its own cancel message).
 *
 * The workflow of the scheduler is that it receives "min-threads" threads
 * during startup, and then if a "leader" thread, (typically the first thread
 * added) feels it is getting behind, it will ask the scheduler manager for
 * more help.  The scheduler manager will try to first repurpose
 * existing threads, or create new threads if no existing threads are
 * available.  Based on the speed of work performed by the scheduler,
 * the manager will try to appropriate a sufficient number of threads.
 * When destroyed, the scheduler will block until all of its threads have
 * worked through their queues.
 *
 * The average user should not need to use the functions here; the #IrisTask
 * and #IrisProcess objects allow a higher-level way to schedule work
 * asynchronously.
 *
 * All #IrisScheduler methods are safe to call from multiple threads.
 */

G_DEFINE_TYPE (IrisScheduler, iris_scheduler, G_TYPE_OBJECT)

G_LOCK_DEFINE (default_work_scheduler);
G_LOCK_DEFINE (default_control_scheduler);

volatile static IrisScheduler *default_work_scheduler = NULL,
                              *default_control_scheduler = NULL;


/**
 * iris_get_default_control_scheduler:
 *
 * Retrieves the default scheduler used for message processing.
 *
 * Return value: a #IrisScheduler instance
 */
IrisScheduler*
iris_get_default_control_scheduler (void)
{
	if (G_UNLIKELY (default_control_scheduler == NULL)) {
		G_LOCK (default_control_scheduler);
		if (!g_atomic_pointer_get (&default_control_scheduler))
			default_control_scheduler = iris_scheduler_new_full
			                              (1, MAX (2, iris_scheduler_get_n_cpu()));
		G_UNLOCK (default_control_scheduler);
	}
	return g_atomic_pointer_get (&default_control_scheduler);
}

/**
 * iris_set_default_control_scheduler:
 * @scheduler: An #IrisScheduler
 *
 * Allows the caller to set the default scheduler for the process.
 */
void
iris_set_default_control_scheduler (IrisScheduler *new_scheduler)
{
	IrisScheduler *old_scheduler;
	g_return_if_fail (new_scheduler != NULL);

	G_LOCK (default_control_scheduler);
	old_scheduler = g_atomic_pointer_get (&default_control_scheduler);
	g_object_ref (new_scheduler);
	g_atomic_pointer_set (&default_control_scheduler, new_scheduler);
	G_UNLOCK (default_control_scheduler);

	if (old_scheduler)
		g_object_unref ((gpointer)old_scheduler);
}

/**
 * iris_get_default_work_scheduler:
 *
 * Retrieves the default scheduler used for tasks and processes.
 *
 * Return value: a #IrisScheduler instance
 */
IrisScheduler*
iris_get_default_work_scheduler (void)
{
	if (G_UNLIKELY (default_work_scheduler == NULL)) {
		G_LOCK (default_work_scheduler);
		if (!g_atomic_pointer_get (&default_work_scheduler))
			default_work_scheduler = iris_scheduler_new_full
			                              (MAX (2, iris_scheduler_get_n_cpu()),
			                               iris_scheduler_get_n_cpu() * 2);
		G_UNLOCK (default_work_scheduler);
	}
	return g_atomic_pointer_get (&default_work_scheduler);
}

/**
 * iris_set_default_work_scheduler:
 * @scheduler: An #IrisScheduler
 *
 * Allows the caller to set the default work scheduler.
 */
void
iris_set_default_work_scheduler (IrisScheduler *new_scheduler)
{
	IrisScheduler *old_scheduler;
	g_return_if_fail (new_scheduler != NULL);

	G_LOCK (default_work_scheduler);
	old_scheduler = g_atomic_pointer_get (&default_work_scheduler);
	g_object_ref (new_scheduler);
	g_atomic_pointer_set (&default_work_scheduler, new_scheduler);
	G_UNLOCK (default_work_scheduler);

	if (old_scheduler)
		g_object_unref ((gpointer)old_scheduler);
}

static gboolean
iris_scheduler_queue_rrobin_cb (gpointer data,
                                gpointer user_data)
{
	IrisQueue      *queue;
	IrisThreadWork *thread_work;

	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (user_data != NULL, FALSE);

	queue = data;
	thread_work = user_data;

	/* If the queue is closed (meaning thread has finished) we will return
	 * FALSE and the rrobin will call again with another queue
	 */
	return iris_queue_push (queue, thread_work);
}

static void
iris_scheduler_queue_real (IrisScheduler  *scheduler,
                           IrisCallback    func,
                           gpointer        data,
                           GDestroyNotify  destroy_notify)
{
	IrisSchedulerPrivate *priv;
	IrisThreadWork       *thread_work;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (func != NULL);

	priv = scheduler->priv;

	thread_work = iris_thread_work_new (func, data, destroy_notify);

	iris_rrobin_apply (priv->rrobin, iris_scheduler_queue_rrobin_cb, thread_work);
}

static gboolean
iris_scheduler_unqueue_real (IrisScheduler *scheduler,
                             gpointer       work_item)
{
	IrisThreadWork *thread_work = (IrisThreadWork *)work_item;
	gboolean        work_claimed;

	g_return_val_if_fail (scheduler != NULL, FALSE);
	g_return_val_if_fail (work_item != NULL, FALSE);

	g_atomic_int_set (&thread_work->remove, TRUE);

	work_claimed = g_atomic_int_compare_and_exchange (&thread_work->taken, FALSE, TRUE);

	if (!work_claimed) {
		/* Thread has claimed the work; the thread will free the work. */
		return FALSE;
	} else {
		/* Work will be freed when found by a foreach, or by a thread when it
		 * is popped. Either way we can guarantee the work will not run.
		 */
		return TRUE;
	}
}

typedef struct {
	IrisScheduler            *scheduler;
	IrisSchedulerForeachFunc  callback;
	gpointer                  user_data;
} IrisSchedulerForeachClosure;

static gboolean
iris_scheduler_foreach_rrobin_cb (IrisRRobin *rrobin,
                                  gpointer    data,
                                  gpointer    user_data)
{
	IrisQueue                    *queue   = data;
	IrisSchedulerForeachClosure  *closure = user_data;
	gboolean continue_flag = TRUE;
	gint i;

	/* Foreach the queue in a really hacky way. FIXME: be neater!
	 * And make sure the order of work is preserved!!!
	 * In particular, avoid calling queue_length() each time! */
	for (i=0; i<iris_queue_get_length(queue); i++) {
		IrisThreadWork *thread_work = iris_queue_try_pop (queue);
		/* By removing the work from the queue, we know now it can't be executed */

		if (!thread_work)
			break;

		continue_flag = closure->callback (closure->scheduler,
		                                   thread_work,
		                                   thread_work->callback,
		                                   thread_work->data,
		                                   closure->user_data);

		if (g_atomic_int_get (&thread_work->remove) == FALSE)
			iris_queue_push (queue, thread_work);
		else
			iris_thread_work_free (thread_work);

		if (!continue_flag)
			break;
	}

	return continue_flag;
}

static void
iris_scheduler_foreach_real (IrisScheduler            *scheduler,
                             IrisSchedulerForeachFunc  callback,
                             gpointer                  user_data)
{
	IrisSchedulerPrivate        *priv;
	IrisSchedulerForeachClosure  closure;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (callback != NULL);

	priv = scheduler->priv;

	closure.scheduler = scheduler;
	closure.callback  = callback;
	closure.user_data = user_data;

	/* Iterate through each of our queues */
	iris_rrobin_foreach (priv->rrobin,
	                     iris_scheduler_foreach_rrobin_cb,
	                     &closure);
};

static gint
iris_scheduler_get_min_threads_real (IrisScheduler *scheduler)
{
	gint min_threads;
	min_threads = scheduler->priv->min_threads;
	return (min_threads > 0) ? min_threads : 2;
}

static gint
iris_scheduler_get_max_threads_real (IrisScheduler *scheduler)
{
	IrisSchedulerPrivate *priv = scheduler->priv;
	if (G_UNLIKELY (priv->max_threads == 0))
		priv->max_threads = MAX(2, iris_scheduler_get_n_cpu () * 2);
	return priv->max_threads;
}

static void
iris_scheduler_add_thread_real (IrisScheduler  *scheduler,
                                IrisThread     *thread,
                                gboolean        exclusive)
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

	/* FIXME: No synchronisation !!! */
	priv->thread_list = g_list_prepend (priv->thread_list, thread);

	/* check if this thread is the leader */
	leader = g_atomic_int_compare_and_exchange (&priv->has_leader, FALSE, TRUE);

	g_warn_if_fail (leader==FALSE || exclusive==TRUE);

	iris_thread_manage (thread, queue, exclusive, leader);

	return;

error:
	g_object_unref (queue);
	thread->user_data = NULL;
}

static void
iris_scheduler_remove_thread_real (IrisScheduler *scheduler,
                                   IrisThread    *thread)
{
	IrisSchedulerPrivate *priv;
	IrisQueue            *work_queue;

	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	priv = scheduler->priv;

	g_mutex_lock (thread->mutex);
	work_queue = thread->user_data;

	g_warn_if_fail (IRIS_IS_QUEUE (work_queue));
	g_warn_if_fail (iris_queue_is_closed (work_queue));

	iris_rrobin_remove (priv->rrobin, work_queue);

	thread->user_data = NULL;

	priv->thread_list = g_list_remove (priv->thread_list, thread);

	/* We don't check, but this thread should not be the leader because the
	 * leader should be running in exclusive mode ...
	 */

	g_mutex_unlock (thread->mutex);
}

static void
iris_scheduler_iterate_real (IrisScheduler *scheduler) {
	/* No-op, this is only used by IrisGMainScheduler, but since this function
	 * is to be called while waiting for the scheduler to process something a
	 * yield won't go amiss. */
	g_thread_yield ();
}

static void
release_thread (gpointer data,
                gpointer user_data)
{
	IrisThread    *thread = data;
	IrisScheduler *scheduler = IRIS_SCHEDULER (user_data);
	IrisQueue     *work_queue;

	g_mutex_lock (thread->mutex);

	/* thread->active could == NULL if the thread has been added but not yet
	 * started work */
	work_queue = thread->user_data;

	/* If the thread has stopped working for us it should have called
	 * iris_scheduler_remove_thread() already
	 */
	g_warn_if_fail (g_atomic_pointer_get (&thread->scheduler) == scheduler);

	iris_queue_close (work_queue);

	g_mutex_unlock (thread->mutex);

	/* Wait for the thread to recognise its removal, to avoid it accessing
	 * freed memory.
	 */
	while (g_atomic_pointer_get (&thread->scheduler) != NULL) {
		g_warn_if_fail (iris_queue_is_closed (work_queue));

		g_thread_yield ();
	}

	g_object_unref (work_queue);
}

static void
iris_scheduler_finalize (GObject *object)
{
	IrisScheduler        *scheduler;
	IrisSchedulerPrivate *priv;

	scheduler = IRIS_SCHEDULER (object);
	priv = scheduler->priv;

	/* For the benefit of our threads */
	g_atomic_int_set (&scheduler->in_finalize, TRUE);

	g_mutex_lock (priv->mutex);

	/* Release all of our threads */
	g_list_foreach (priv->thread_list, release_thread, scheduler);
	g_list_free (priv->thread_list);

	g_mutex_unlock (priv->mutex);

	if (priv->rrobin != NULL)
		iris_rrobin_unref (priv->rrobin);

	g_mutex_free (priv->mutex);

	G_OBJECT_CLASS (iris_scheduler_parent_class)->finalize (object);
}

static void
iris_scheduler_class_init (IrisSchedulerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->queue = iris_scheduler_queue_real;
	klass->unqueue = iris_scheduler_unqueue_real;
	klass->foreach = iris_scheduler_foreach_real;
	klass->get_min_threads = iris_scheduler_get_min_threads_real;
	klass->get_max_threads = iris_scheduler_get_max_threads_real;
	klass->add_thread = iris_scheduler_add_thread_real;
	klass->remove_thread = iris_scheduler_remove_thread_real;
	klass->iterate = iris_scheduler_iterate_real;

	object_class->finalize = iris_scheduler_finalize;

	g_type_class_add_private (object_class, sizeof (IrisSchedulerPrivate));
}

static void
iris_scheduler_init (IrisScheduler *scheduler)
{
	scheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheduler,
	                                          IRIS_TYPE_SCHEDULER,
	                                          IrisSchedulerPrivate);

	scheduler->in_finalize = FALSE;
	scheduler->maxed = FALSE;

	scheduler->priv->mutex = g_mutex_new ();
	scheduler->priv->queue = g_async_queue_new ();

	scheduler->priv->thread_list = NULL;

	scheduler->priv->min_threads = 0;
	scheduler->priv->max_threads = 0;

	/* Actual init happens lazily from iris_scheduler_queue() */
	scheduler->priv->initialized = FALSE;
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

/**
 * iris_scheduler_queue:
 * @scheduler: An #IrisScheduler
 * @func: An #IrisCallback
 * @data: data for @func
 * @destroy_notify: an optional callback after execution to free data
 *
 * Queues a new work item to be executed by one of the scheduler's work
 * threads.
 *
 * @destroy_notify, if non-%NULL, should <emphasis>only</emphasis> handle
 * freeing data. If the work is unqueued and does not run, @destroy_notify will
 * still be called, and could potentially not execute for a long time after
 * @func completes.
 *
 * The order in which the items will be executed is impossible to
 * guarantee, since threads can preempt each other at any point. One way to
 * ensure ordered processing is to use an #IrisReceiver that has been set as
 * 'exclusive' using iris_arbiter_coordinate(), so that only one message will
 * be processed at a time (and in the order that they were posted).
 *
 */
void
iris_scheduler_queue (IrisScheduler  *scheduler,
                      IrisCallback    func,
                      gpointer        data,
                      GDestroyNotify  destroy_notify)
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

	IRIS_SCHEDULER_GET_CLASS (scheduler)->queue (scheduler, func, data, destroy_notify);
}

/**
 * iris_scheduler_unqueue:
 * @scheduler: An #IrisScheduler
 * @work_item: An opaque pointer identifying the work item. This can currently
 *             only be obtained using iris_scheduler_foreach().
 *
 * Tries to abort and free a piece of work that was previously queued. This may
 * or may not be possible. The work will not execute after this function
 * returns, but may already be in progress, in which case it is up to the
 * caller to wait for it to finish.
 *
 * This function guarantees that the work item will be freed (including calling
 * its destroy notify) but this might not occur immediately.
 *
 * Return value: %TRUE if the attempt to unqueue was successful, %FALSE if the
 *               work ran or is still running.
 */
gboolean
iris_scheduler_unqueue (IrisScheduler  *scheduler,
                        gpointer        work_item)
{
	IrisSchedulerPrivate *priv;

	g_return_val_if_fail (scheduler != NULL, FALSE);

	priv = scheduler->priv;

	g_return_val_if_fail (priv->initialized, FALSE);

	return IRIS_SCHEDULER_GET_CLASS (scheduler)->unqueue (scheduler, work_item);
}

/**
 * iris_scheduler_foreach:
 * @scheduler: An #IrisScheduler
 * @callback: An #IrisSchedulerForeachFunc
 * @user_data: data for @func
 *
 * Calls @callback for each piece of queued work in @scheduler. @callback
 * receives as its parameters the callback function and data for the work item,
 * plus @user_data.
 *
 * The return value of @callback should be %FALSE to stop the foreach or %TRUE
 * to continue execution.
 */
void iris_scheduler_foreach (IrisScheduler            *scheduler,
                             IrisSchedulerForeachFunc  callback,
                             gpointer                  user_data)
{
	IrisSchedulerPrivate *priv;

	g_return_if_fail (scheduler != NULL);

	priv = scheduler->priv;

	if (G_UNLIKELY (!priv->initialized))
		return;

	IRIS_SCHEDULER_GET_CLASS (scheduler)->foreach (scheduler,
	                                               callback,
	                                               user_data);
}


/**
 * iris_scheduler_get_max_threads:
 * @scheduler: An #IrisScheduler
 *
 * Retrieves the maximum number of threads the scheduler should be allocated.
 * The default is equal to the number of cpus unless there is only a single
 * cpu, in which case the default is 2.
 *
 * See iris_scheduler_get_n_cpu() for more information.
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
 * @exclusive: Whether the thread belongs to the scheduler, or is just being
 *             added temporarily to clear a backlog of work
 *
 * Requests that the scheduler add the thread to its set of executing
 * threads. It is the responsibility of the scheduler to tell the thread
 * to start managing a work queue with iris_thread_manage().
 */
void
iris_scheduler_add_thread (IrisScheduler *scheduler,
                           IrisThread    *thread,
                           gboolean       exclusive)
{
	IRIS_SCHEDULER_GET_CLASS (scheduler)->add_thread (scheduler, thread, exclusive);
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

	/* We know that the scheduler definitely is no longer
	 * maxed out since this thread is ending.
	 */
	g_atomic_int_set (&scheduler->maxed, FALSE);
}

/**
 * iris_scheduler_get_n_cpu:
 *
 * Returns the number of processor cores identified by the system.
 * This operation is currently supported on Linux, Mac OS X and MS Windows.
 * If you would like another OS supported, please send an email with the
 * method to retreive the number of cpus (get_nprocs() on Linux).
 *
 * Return value: number of processor cores identified by the system.
 */
guint
iris_scheduler_get_n_cpu (void)
{
	static gint n_cpu = 0;
	if (G_UNLIKELY (!n_cpu)) {
		if (g_getenv ("IRIS_SCHED_MAX") != NULL)
			n_cpu = atoi (g_getenv ("IRIS_SCHED_MAX"));
#ifdef LINUX
		if (n_cpu == 0)
			n_cpu = get_nprocs ();
#elif DARWIN
		size_t size = sizeof (n_cpu);
		if (sysctlbyname ("hw.ncpu", &n_cpu, &size, NULL, 0))
			n_cpu = 1;
#elif defined(WIN32)
		SYSTEM_INFO info;
		GetSystemInfo (&info);
		n_cpu = info.dwNumberOfProcessors;
#else
		n_cpu = 1;
#endif
	}
	return n_cpu;
}
