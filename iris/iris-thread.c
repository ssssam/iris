/* iris-thread.c
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

#include <glib/gprintf.h>

#include "iris-message.h"
#include "iris-queue.h"
#include "iris-scheduler-manager.h"
#include "iris-scheduler-manager-private.h"
#include "iris-thread.h"
#include "iris-util.h"

#define MSG_MANAGE       (1)
#define MSG_SHUTDOWN     (2)
#define QUANTUM_USECS    (G_USEC_PER_SEC * 1)
#define POP_WAIT_TIMEOUT (G_USEC_PER_SEC * 2)

__thread IrisThread* my_thread = NULL;

static gboolean
timeout_elapsed (GTimeVal *tv1,
                 GTimeVal *tv2)
{
	GTimeVal tv_quantum = *tv1;
	g_time_val_add (&tv_quantum, QUANTUM_USECS);
	return (g_time_val_compare (&tv_quantum, tv2) == 1);
}

static void
iris_thread_worker_exclusive (IrisThread  *thread,
                              IrisQueue   *queue,
                              gboolean     leader)
{
	GTimeVal        tv_now      = {0,0};
	GTimeVal        tv_req      = {0,0};
	IrisThreadWork *thread_work = NULL;
	gint            per_quantum = 0;     /* Completed items within the
	                                      * last quantum.
	                                      */
	guint           count       = 0;     /* Items left in the queue at */
	guint           tmp         = 0;     /* the last check.            */
	gboolean        growing     = FALSE; /* If we are growing faster than
	                                      * than we can process items.
	                                      */

	g_get_current_time (&tv_now);
	g_get_current_time (&tv_req);
	count = iris_queue_length (queue);

	/* Since our thread is in exclusive mode, we are responsible for
	 * asking the scheduler manager to add or remove threads based
	 * on the demand of our work queue.
	 *
	 * If the scheduler has maxed out the number of threads it is
	 * allowed, then we will not ask the scheduler to add more
	 * threads and rebalance.
	 */

get_next_item:

	if (G_LIKELY ((thread_work = iris_queue_pop (queue)) != NULL)) {
		iris_thread_work_run (thread_work);
		iris_thread_work_free (thread_work);
		per_quantum++;
	}
	else {
#if 0
		g_warning ("Exclusive thread is done managing, received NULL");
		return;
#endif
	}

	if (G_UNLIKELY (!thread->scheduler->maxed && leader)) {
		g_get_current_time (&tv_now);

		if (G_UNLIKELY (timeout_elapsed (&tv_req, &tv_now))) {
			tmp = iris_queue_length (queue);
			growing = tmp > 0 && tmp > count;
			count = tmp;

			/* if we will be done within one more quantum, then its
			 * not essential to add the other thread.
			 */
			if (per_quantum < count) {
				/* make sure we are not maxed before asking */
				if (!g_atomic_int_get (&thread->scheduler->maxed))
					iris_scheduler_manager_request (thread->scheduler,
									per_quantum,
									count);
			}

			per_quantum = 0;
			tv_req = tv_now;
		}
	}

	goto get_next_item;
}

static void
iris_thread_worker_transient (IrisThread  *thread,
                              IrisQueue   *queue)
{
	IrisThreadWork *thread_work = NULL;
	GTimeVal        tv_timeout = {0,0};

	/* The transient mode worker is responsible for helping finish off as
	 * many of the work items as fast as possible.  It is not responsible
	 * for asking for more helpers, just processing work items.  When done
	 * processing work items, it will yield itself back to the scheduler
	 * manager.
	 */

	do {
		g_get_current_time (&tv_timeout);
		g_time_val_add (&tv_timeout, POP_WAIT_TIMEOUT);

		if ((thread_work = iris_queue_timed_pop (queue, &tv_timeout)) != NULL) {
			iris_thread_work_run (thread_work);
			iris_thread_work_free (thread_work);
		}
	} while (thread_work != NULL);

	/* Yield our thread back to the scheduler manager */
	iris_scheduler_manager_yield (thread);
}

static void
iris_thread_handle_manage (IrisThread  *thread,
                           IrisQueue   *queue,
                           gboolean     exclusive,
                           gboolean     leader)
{
	g_return_if_fail (queue != NULL);

	g_mutex_lock (thread->mutex);
	thread->active = iris_queue_ref (queue);
	g_mutex_unlock (thread->mutex);

	if (G_UNLIKELY (exclusive))
		iris_thread_worker_exclusive (thread, queue, leader);
	else
		iris_thread_worker_transient (thread, queue);

	g_mutex_lock (thread->mutex);
	thread->active = NULL;
	g_mutex_unlock (thread->mutex);
}

static void
iris_thread_handle_shutdown (IrisThread *thread)
{
}

static gpointer
iris_thread_worker (IrisThread *thread)
{
	IrisMessage *message;

	g_return_val_if_fail (thread != NULL, NULL);
	g_return_val_if_fail (thread->queue != NULL, NULL);

	my_thread = thread;

next_message:
	message = g_async_queue_pop (thread->queue);

	if (!message)
		return NULL;

	switch (message->what) {
	case MSG_MANAGE:
		iris_thread_handle_manage (thread,
		                           iris_message_get_pointer (message, "queue"),
		                           iris_message_get_boolean (message, "exclusive"),
		                           iris_message_get_boolean (message, "leader"));
		break;
	case MSG_SHUTDOWN:
		iris_thread_handle_shutdown (thread);
		break;
	default:
		g_assert_not_reached ();
	}

	goto next_message;
}

/**
 * iris_thread_new:
 * @exclusive: the thread is exclusive
 *
 * Createa a new #IrisThread instance that can be used to queue work items
 * to be processed on the thread.
 *
 * If @exclusive, then the thread will not yield to the scheduler and
 * therefore will not participate in scheduler thread balancing.
 *
 * Return value: the newly created #IrisThread instance
 */
IrisThread*
iris_thread_new (gboolean exclusive)
{
	IrisThread *thread;

#if 0
	g_debug ("%s(exclusive=%s)", __func__, exclusive ? "TRUE" : "FALSE");
#endif

	thread = g_slice_new0 (IrisThread);
	thread->exclusive = exclusive;
	thread->queue = g_async_queue_new ();
	thread->mutex = g_mutex_new ();
	thread->thread  = g_thread_create_full ((GThreadFunc)iris_thread_worker,
	                                        thread,
	                                        0,     /* stack size    */
	                                        FALSE, /* joinable      */
	                                        FALSE, /* system thread */
	                                        G_THREAD_PRIORITY_NORMAL,
	                                        NULL);
	thread->scheduler = NULL;

	return thread;
}

/**
 * iris_thread_get:
 *
 * Retrieves the pointer to the current threads structure.
 *
 * Return value: the threads structure or NULL if not an #IrisThread.
 */
IrisThread*
iris_thread_get (void)
{
	return my_thread;
}

/**
 * iris_thread_manage:
 * @thread: An #IrisThread
 * @queue: A #GAsyncQueue
 * @leader: If the thread is responsible for asking for more threads
 *
 * Sends a message to the thread asking it to retreive work items from
 * the queue.
 *
 * If @leader is %TRUE, then the thread will periodically ask the scheduler
 * manager to ask for more threads.
 */
void
iris_thread_manage (IrisThread    *thread,
                    IrisQueue     *queue,
                    gboolean       leader)
{
	IrisMessage *message;

	g_return_if_fail (thread != NULL);
	g_return_if_fail (queue != NULL);

#if 0
	g_debug ("%s(leader=%s)", __func__, leader ? "TRUE" : "FALSE");
#endif

	message = iris_message_new_full (MSG_MANAGE,
	                                 "exclusive", G_TYPE_BOOLEAN, thread->exclusive,
	                                 "queue", G_TYPE_POINTER, queue,
	                                 "leader", G_TYPE_BOOLEAN, leader,
	                                 NULL);
	g_async_queue_push (thread->queue, message);
}

/**
 * iris_thread_shutdown:
 * @thread: An #IrisThread
 *
 * Sends a message to the thread asking it to shutdown.
 */
void
iris_thread_shutdown (IrisThread *thread)
{
	IrisMessage *message;

	g_return_if_fail (thread != NULL);

	message = iris_message_new (MSG_SHUTDOWN);
	g_async_queue_push (thread->queue, message);
}

/**
 * iris_thread_print_stat:
 * @thread: An #IrisThread
 *
 * Prints the stats of an #IrisThread to standard output for analysis.
 * See iris_thread_stat() for programmatically access the statistics.
 */
void
iris_thread_print_stat (IrisThread *thread)
{
	g_mutex_lock (thread->mutex);
	g_fprintf (stderr,
	           "    Thread 0x%08lx     Active: %s     Queue Size: %d\n",
	           (long)thread->thread,
	           thread->active != NULL ? "yes" : "no",
	           thread->active != NULL ? iris_queue_length (thread->active) : 0);
	g_mutex_unlock (thread->mutex);
}

/**
 * iris_thread_work_new:
 * @callback: An #IrisCallback
 * @data: user supplied data
 *
 * Creates a new instance of #IrisThreadWork, which is the negotiated contract
 * between schedulers and the thread workers themselves.
 *
 * Return value: The newly created #IrisThreadWork instance.
 */
IrisThreadWork*
iris_thread_work_new (IrisCallback callback,
                      gpointer     data)
{
	IrisThreadWork *thread_work;

	thread_work = g_slice_new (IrisThreadWork);
	thread_work->callback = callback;
	thread_work->data = data;

	return thread_work;
}

/**
 * iris_thread_work_run:
 * @thread_work: An #IrisThreadWork
 *
 * Executes the thread work. This method is called from within the worker
 * thread.
 */
void
iris_thread_work_run (IrisThreadWork *thread_work)
{
	g_return_if_fail (thread_work != NULL);
	g_return_if_fail (thread_work->callback != NULL);
	thread_work->callback (thread_work->data);
}

/**
 * iris_thread_work_free:
 * @thread_work: An #IrisThreadWork
 *
 * Frees the resources associated with an #IrisThreadWork.
 */
void
iris_thread_work_free (IrisThreadWork *thread_work)
{
	thread_work->callback = NULL;
	g_slice_free (IrisThreadWork, thread_work);
}
