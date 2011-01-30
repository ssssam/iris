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

#include <glib.h>
#include <glib/gprintf.h>

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include "iris-debug.h"
#include "iris-message.h"
#include "iris-queue.h"
#include "iris-scheduler-manager.h"
#include "iris-scheduler-manager-private.h"
#include "iris-util.h"

/**
 * SECTION:iris-thread
 * @title: IrisThread
 * @short_description: Thread abstraction for schedulers
 *
 * #IrisThread provides an abstraction upon the underlying threading
 * system for use by #IrisScheduler implementations.
 */

#define MSG_MANAGE            (1)
#define MSG_SHUTDOWN          (2)
#define QUANTUM_USECS         (G_USEC_PER_SEC / 1)
#define POP_WAIT_TIMEOUT      (G_USEC_PER_SEC * 2)
#define VERIFY_THREAD_WORK(t) (g_atomic_int_compare_and_exchange(&t->taken, FALSE, TRUE))

#if LINUX
__thread IrisThread* my_thread = NULL;
#elif defined(WIN32)
static GOnce my_thread_once = G_ONCE_INIT;
static DWORD my_thread;
#else
static pthread_once_t my_thread_once = PTHREAD_ONCE_INIT;
static pthread_key_t my_thread;
#endif

static gboolean
timeout_elapsed (GTimeVal *start,
                 GTimeVal *end)
{
	GTimeVal end_by = *start;
	g_time_val_add (&end_by, QUANTUM_USECS);
	return (g_time_val_compare (&end_by, end) == 1);
}

static void
iris_thread_worker_exclusive (IrisThread  *thread,
                              IrisQueue   *queue,
                              gboolean     leader)
{
	GTimeVal        tv_now      = {0,0};
	GTimeVal        tv_req      = {0,0};
	IrisThreadWork *thread_work = NULL;
	gint            per_quanta = 0;      /* Completed items within the
	                                      * last quanta. */
	guint           queued      = 0;     /* Items left in the queue at */
	gboolean        has_resized = FALSE;

	iris_debug (IRIS_DEBUG_THREAD);

	g_get_current_time (&tv_now);
	g_get_current_time (&tv_req);
	queued = iris_queue_length (queue);

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
		if (!VERIFY_THREAD_WORK (thread_work)) {
			g_warning ("Invalid thread_work %lx\n", (gulong)thread_work);
			goto get_next_item;
		}

		iris_thread_work_run (thread_work);
		iris_thread_work_free (thread_work);
		per_quanta++;
	}
	else {
		/* This should not be possible since iris_queue_pop cannot return NULL */
		g_warning ("Exclusive thread is done managing, received NULL");
		return;
	}

	if (G_UNLIKELY (!thread->scheduler->maxed && leader)) {
		g_get_current_time (&tv_now);

		if (G_UNLIKELY (timeout_elapsed (&tv_now, &tv_req))) {
			/* We check to see if we have a bunch more work to do
			 * or a potential edge case where we are processing about
			 * the same speed as the pusher, but it creates enough
			 * contention where we dont speed up. This is because
			 * some schedulers will round-robin or steal.  And unless
			 * we look to add another thread even though we have nothing
			 * in the queue, we know there are more coming.
			 */
			queued = iris_queue_length (queue);
			if (queued == 0 && !has_resized) {
				queued = per_quanta * 2;
				has_resized = TRUE;
			}

			if (per_quanta < queued) {
				/* make sure we are not maxed before asking */
				if (!g_atomic_int_get (&thread->scheduler->maxed))
					iris_scheduler_manager_request (thread->scheduler,
									per_quanta,
									queued);
			}

			per_quanta = 0;
			tv_req = tv_now;
			g_time_val_add (&tv_req, QUANTUM_USECS);
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

	iris_debug (IRIS_DEBUG_THREAD);

	/* The transient mode worker is responsible for helping finish off as
	 * many of the work items as fast as possible.  It is not responsible
	 * for asking for more helpers, just processing work items.  When done
	 * processing work items, it will yield itself back to the scheduler
	 * manager.
	 */

	do {
		g_get_current_time (&tv_timeout);
		g_time_val_add (&tv_timeout, POP_WAIT_TIMEOUT);

		if ((thread_work = iris_queue_timed_pop_or_close (queue, &tv_timeout)) != NULL) {
			if (!VERIFY_THREAD_WORK (thread_work))
				continue;
			iris_thread_work_run (thread_work);
			iris_thread_work_free (thread_work);
		}
	} while (thread_work != NULL);

	/* Yield our thread back to the scheduler manager */
	iris_scheduler_manager_yield (thread);
	thread->scheduler = NULL;
}

static void
iris_thread_handle_manage (IrisThread  *thread,
                           IrisQueue   *queue,
                           gboolean     exclusive,
                           gboolean     leader)
{
	g_return_if_fail (queue != NULL);

	g_mutex_lock (thread->mutex);
	thread->active = g_object_ref (queue);
	g_mutex_unlock (thread->mutex);

	thread->exclusive = exclusive;

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
	GTimeVal     timeout = {0,0};

	g_return_val_if_fail (thread != NULL, NULL);
	g_return_val_if_fail (thread->queue != NULL, NULL);

#if LINUX
	my_thread = thread;
#elif defined(WIN32)
	TlsSetValue (my_thread, thread);
#else
	pthread_setspecific (my_thread, thread);
#endif

	iris_debug_init_thread ();
	iris_debug (IRIS_DEBUG_THREAD);

next_message:
	if (thread->exclusive) {
		message = g_async_queue_pop (thread->queue);
	}
	else {
		/* If we do not get any schedulers to work for within our
		 * timeout period, we can safely shutdown. */
		g_get_current_time (&timeout);
		g_time_val_add (&timeout, G_USEC_PER_SEC * 5);
		message = g_async_queue_timed_pop (thread->queue, &timeout);

		if (!message) {
			/* Make sure that the manager removes us from the free thread list.
			 * The manager can return FALSE to prevent shutdown if it has
			 * decided to give us new work.
			 */
			if (!iris_scheduler_manager_destroy (thread))
				goto next_message;

			/* make sure nothing was added while we
			 * removed ourselves */
			message = g_async_queue_try_pop (thread->queue);
		}
	}

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
		g_warn_if_reached ();
		break;
	}

	goto next_message;
}

GType
iris_thread_get_type (void)
{
	static GType thread_type = 0;
	if (G_UNLIKELY (!thread_type))
		thread_type = g_pointer_type_register_static ("IrisThread");
	return thread_type;
}

#if LINUX
#elif defined(WIN32)
static void
_winthreads_init (void)
{
	/* FIXME: is it a problem that this is not freed? */
	my_thread = TlsAlloc ();
}
#else
static void
_pthread_init (void)
{
	pthread_key_create (&my_thread, NULL);
}
#endif

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

	iris_debug (IRIS_DEBUG_THREAD);

#if LINUX
#elif defined(WIN32)
	g_once (&my_thread_once, (GThreadFunc)_winthreads_init, NULL);
#else
	pthread_once (&my_thread_once, _pthread_init);
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
#if LINUX
	return my_thread;
#elif defined(WIN32)
	return TlsGetValue (my_thread);
#else
	return pthread_getspecific (my_thread);
#endif
}

/**
 * iris_thread_manage:
 * @thread: An #IrisThread
 * @queue: A #GAsyncQueue
 * @exclusive: Whether the thread should run in exclusive mode
 * @leader: If the thread is responsible for asking for more threads
 *
 * Sends a message to the thread asking it to retreive work items from
 * the queue.
 *
 * If @exclusive is %TRUE, the thread will watch @queue for work items
 * indefinitely. If it is %FALSE, the thread runs in transient mode - once
 * @queue is empty it will remove itself from the calling scheduler and yield
 * itself back to the scheduler manager.
 *
 * If @leader is %TRUE, then the thread will periodically ask the scheduler
 * manager to ask for more threads.
 */
void
iris_thread_manage (IrisThread    *thread,
                    IrisQueue     *queue,
                    gboolean       exclusive,
                    gboolean       leader)
{
	IrisMessage *message;

	g_return_if_fail (thread != NULL);
	g_return_if_fail (queue != NULL);

	iris_debug (IRIS_DEBUG_THREAD);

	message = iris_message_new_full (MSG_MANAGE,
	                                 "exclusive", G_TYPE_BOOLEAN, exclusive,
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

	iris_debug (IRIS_DEBUG_THREAD);

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
	iris_debug (IRIS_DEBUG_THREAD);

	g_mutex_lock (thread->mutex);

	g_fprintf (stderr,
	           "    Thread 0x%016lx     Sched 0x%016lx %s Work q. 0x%016lx\n"
	           "\t  Active: %3s     Queue Size: %d\n",
	           (long)thread->thread,
	           (long)thread->scheduler,
	           thread->scheduler==iris_scheduler_default()? "(def.)  ": "        ",
	           (long)thread->active,
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
	thread_work->taken = FALSE;

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

/**
 * iris_thread_is_working:
 * @thread: An #IrisThread
 *
 * Checks to see if a thread is currently processing work items from a queue.
 * Keep in mind this is always a race condition.  It is primarily useful for
 * schedulers to know if a thread they are running in is active.
 *
 * Return value: %TRUE if the thread is currently working on a queue.
 */
gboolean
iris_thread_is_working (IrisThread *thread)
{
	g_return_val_if_fail (thread != NULL, FALSE);
	return (thread->active != NULL);
}
