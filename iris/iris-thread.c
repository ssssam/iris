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

#include "iris-message.h"
#include "iris-scheduler-manager-private.h"
#include "iris-thread.h"

#define MSG_MANAGE   1
#define MSG_SHUTDOWN 2

#define POP_WAIT_TIMEOUT (G_USEC_PER_SEC * 2)

static void
iris_thread_handle_manage (IrisThread  *thread,
                           GAsyncQueue *queue,
                           gboolean     exclusive)
{
	IrisThreadWork *work;
	GTimeVal        tv;
	gboolean        cleanup = FALSE;
	gint            timeout = POP_WAIT_TIMEOUT;

	g_return_if_fail (queue != NULL);

next_item:

	if (exclusive) {
		work = g_async_queue_pop (queue);
	}
	else {
		g_get_current_time (&tv);
		g_time_val_add (&tv, timeout);
		work = g_async_queue_timed_pop (queue, &tv);
	}

	if (work) {
		iris_thread_work_run (work);
		iris_thread_work_free (work);
		goto next_item;
	}

	/* If we havent already been yielded then we will ask the scheduler
	 * manager if there it would like to replace us into another
	 * scheduler.  If so TRUE is returned and we need to finish any new
	 * work items that have come in during our time in yield.
	 */
	if (!cleanup && iris_scheduler_manager_yield (thread)) {
		exclusive = FALSE;
		cleanup = TRUE;
		timeout = 0;
		goto next_item;
	}
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

next_message:
	message = g_async_queue_pop (thread->queue);

	if (!message)
		return NULL;

	switch (message->what) {
	case MSG_MANAGE:
		iris_thread_handle_manage (thread,
		                           iris_message_get_pointer (message, "queue"),
		                           iris_message_get_boolean (message, "exclusive"));
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
 * exclusive: the thread is exclusive
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

	thread = g_slice_new0 (IrisThread);
	thread->exclusive = exclusive;
	thread->queue = g_async_queue_new ();
	thread->thread  = g_thread_create_full ((GThreadFunc)iris_thread_worker,
	                                        thread,
	                                        0,     /* stack size    */
	                                        FALSE, /* joinable      */
	                                        FALSE, /* system thread */
	                                        G_THREAD_PRIORITY_NORMAL,
	                                        NULL);

	return thread;
}

/**
 * iris_thread_manage:
 * @thread: An #IrisThread
 *
 * Sends a message to the thread asking it to retreive work items from
 * the queue.
 */
void
iris_thread_manage (IrisThread  *thread,
                    GAsyncQueue *queue)
{
	IrisMessage *message;

	g_return_if_fail (thread != NULL);
	g_return_if_fail (queue != NULL);

	message = iris_message_new_full (MSG_MANAGE,
	                                 "exclusive", G_TYPE_BOOLEAN, thread->exclusive,
	                                 "queue", G_TYPE_POINTER, queue,
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
	g_slice_free (IrisThreadWork, thread_work);
}
