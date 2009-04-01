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

typedef struct
{
	IrisCallback callback;
	gpointer     data;
} IrisThreadWork;

static void
iris_thread_handle_manage (IrisThread  *thread,
                           GAsyncQueue *queue,
                           gboolean     exclusive)
{
	IrisThreadWork *work;
	GTimeVal        tv;
	gboolean        cleanup = FALSE;
	gint            timeout = POP_WAIT_TIMEOUT;

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
		work->callback (work->data);
		g_slice_free (IrisThreadWork, work);
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

IrisThread*
iris_thread_new (gboolean exclusive)
{
	IrisThread *thread;

	thread = g_slice_new0 (IrisThread);
	thread->thread  = g_thread_create_full ((GThreadFunc)iris_thread_worker,
	                                        thread,
	                                        0,     /* stack size    */
	                                        FALSE, /* joinable      */
	                                        FALSE, /* system thread */
	                                        G_THREAD_PRIORITY_NORMAL,
	                                        NULL);
	thread->exclusive = exclusive;
	thread->queue = g_async_queue_new ();

	return thread;
}

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

void
iris_thread_shutdown (IrisThread *thread)
{
	IrisMessage *message;

	g_return_if_fail (thread != NULL);

	message = iris_message_new (MSG_SHUTDOWN);
	g_async_queue_push (thread->queue, message);
}

void
iris_thread_queue (IrisThread   *thread,
                   GAsyncQueue  *queue,
                   IrisCallback  callback,
                   gpointer      data)
{
	IrisThreadWork *work;

	g_return_if_fail (thread != NULL);
	g_return_if_fail (callback != NULL);

	work = g_slice_new (IrisThreadWork);
	work->callback = callback;
	work->data = data;

	g_async_queue_push (queue, work);
}
