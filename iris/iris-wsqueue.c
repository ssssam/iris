/* iris-wsqueue.c
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

/* This code is based upon the totally awesome-sauce Joe Duffy.  But
 * of course, its all really based upon Nir Shavit's dynamic sized
 * work-stealing queue paper.
 *
 * http://www.bluebytesoftware.com/blog/PermaLink,guid,1665653b-b5f3-49b4-8144-cfbc5e8c632b.aspx
 */

#include <string.h>
#include <pthread.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <time.h>

#include "iris-wsqueue.h"
#include "iris-rrobin.h"
#include "gstamppointer.h"

#define WSQUEUE_DEFAULT_SIZE 32

#if DARWIN
static gboolean
timeout_passed (const struct timespec *timeout)
{
	struct timeval tv;

	gettimeofday (&tv, NULL);

	if (tv.tv_sec > timeout->tv_sec)
		return TRUE;
	else if ((tv.tv_sec == timeout->tv_sec) && (tv.tv_usec >= (timeout->tv_nsec / 1000000)))
		return TRUE;

	return FALSE;
}
static gint
pthread_mutex_timedlock (pthread_mutex_t       *mutex,
                         const struct timespec *abs_timeout)
{
	gint result;

	do {
		result = pthread_mutex_trylock (mutex);
		if (result == EBUSY) {
			struct timespec ts;
			ts.tv_sec = 0;
			ts.tv_nsec = 10000000;

			/* Sleep for 10,000,000 nanoseconds before trying again. */
			int status = -1;
			while (status == -1)
				status = nanosleep (&ts, &ts);
		}
		else
			break;
	}
	while (result != 0 && !timeout_passed (abs_timeout));

	return result;
}
#endif

/**
 * SECTION:iris-wsqueue
 * @short_description: A work-stealing queue
 *
 * #IrisWSQueue is a work-stealing version of #IrisQueue.  It requires access
 * to a global queue for a fallback when out of items.  If no item can be
 * retreived from the global queue, it will try to steal from its peers using
 * the #IrisRRobin of peer queues, which should also be #IrisQueue based.
 */

struct StealInfo
{
	IrisQueue *queue;
	gpointer   result;
};

static void
iris_wsqueue_push_real (IrisQueue *queue,
                        gpointer   data)
{
	/* we only allow pushes from the local thread via local push, so
	 * this should never be hit. */
	g_assert_not_reached ();
}

static gboolean
iris_wsqueue_pop_real_cb (IrisRRobin *rrobin,
                          gpointer    data,
                          gpointer    user_data)
{
	struct StealInfo *steal    = user_data;
	IrisWSQueue      *neighbor = data;

	if (G_LIKELY (steal->queue != data)) {
		if ((steal->result = iris_wsqueue_try_steal (neighbor, 0)) != NULL)
			return FALSE;
	}

	return TRUE;
}

static gpointer
iris_wsqueue_pop_real (IrisQueue *queue)
{
	GTimeVal tv     = {0,0};
	gpointer result = NULL;

	/*
	 * This code path is to only be hit by the thread that owns the Queue!
	 */

	g_get_current_time (&tv);
	if (!(result = iris_queue_timed_pop (queue, &tv))) {
		/* Since only our local thread can push items to our
		 * queue, we can safely block on the global queue now
		 * for a result.
		 */
		result = iris_queue_pop (IRIS_WSQUEUE (queue)->global);
	}

	return result;
}

static gpointer
iris_wsqueue_try_pop_real (IrisQueue *queue)
{
	/*
	 * This code path is to only be hit by the thread that owns the Queue!
	 */

	return iris_wsqueue_pop_real (queue);
}

static gpointer
iris_wsqueue_timed_pop_real (IrisQueue *queue,
                             GTimeVal  *timeout)
{
	/*
	 * This code path is to only be hit by the thread that owns the Queue!
	 */

	struct StealInfo  steal;
	IrisWSQueue      *real_queue;

	g_return_val_if_fail (queue != NULL, NULL);

	real_queue = (IrisWSQueue*)queue;
	steal.queue = queue;
	steal.result = NULL;

	/* We check 3 different queues to retrieve an item through the
	 * public pop interface. First we try to pop locally from our
	 * local queue. Then we check the global queue. If neither of
	 * those have yielded an item, we will try to steal from one
	 * of our neighbors.
	 *
	 * However, so we do not get blocked on the global queue if there
	 * are no items available and we can steal, we look through all
	 * three first (assuming no item was found) and then only do a
	 * timed blocking call on the global queue the second time around.
	 */

	/* Round One */

	if ((steal.result = iris_wsqueue_local_pop (real_queue)) != NULL) {
		return steal.result;
	}

	if ((steal.result = iris_queue_try_pop (real_queue->global)) != NULL) {
		return steal.result;
	}

	iris_rrobin_foreach (real_queue->rrobin,
	                     iris_wsqueue_pop_real_cb,
	                     &steal);

	if (steal.result)
		return steal.result;

	/* Round Two */

	if ((steal.result = iris_queue_timed_pop (real_queue->global, timeout)) != NULL) {
		return steal.result;
	}

	iris_rrobin_foreach (real_queue->rrobin,
	                     iris_wsqueue_pop_real_cb,
	                     &steal);

	return steal.result;
}

static guint
iris_wsqueue_length_real (IrisQueue *queue)
{
	IrisWSQueue *real_queue;
	g_return_val_if_fail (queue != NULL, 0);
	real_queue = (IrisWSQueue*)queue;
	return real_queue->tail_idx - real_queue->head_idx;
}

static void
iris_wsqueue_dispose_real (IrisQueue *queue)
{
	g_slice_free (IrisWSQueue, (IrisWSQueue*)queue);
}

static IrisQueueVTable wsqueue_vtable = {
	iris_wsqueue_push_real,
	iris_wsqueue_pop_real,
	iris_wsqueue_try_pop_real,
	iris_wsqueue_timed_pop_real,
	iris_wsqueue_length_real,
	iris_wsqueue_dispose_real,
};

/**
 * iris_wsqueue_new:
 * @global: An #IrisQueue of global work items
 * @peers: An #IrisRRobin of peer thread #IrisQueue<!-- -->'s
 *
 * Creates a new instance of #IrisWSQueue.
 *
 * A dequeue first tries to retreive from the local queue.  If nothing
 * is available in the queue, we check the global queue.  If nothing is
 * found in the global queue, then we will try to steal an item from a
 * neighbor in the #IrisRRobin.
 *
 * Return value: The newly created #IrisWSQueue instance.
 */
IrisQueue*
iris_wsqueue_new (IrisQueue  *global,
                  IrisRRobin *peers)
{
	IrisWSQueue *queue;

	queue = g_slice_new0 (IrisWSQueue);

	queue->parent.vtable = &wsqueue_vtable;
	queue->parent.ref_count = 1;

	if (global)
		queue->global = iris_queue_ref (global);

	queue->rrobin = peers;
	queue->mask = WSQUEUE_DEFAULT_SIZE - 1;
	queue->mutex = g_mutex_new ();
	queue->length = WSQUEUE_DEFAULT_SIZE;
	queue->items = g_malloc0 (sizeof (gpointer) * queue->length);
	queue->head_idx = 0;
	queue->tail_idx = 0;

	return (IrisQueue*)queue;
}

/**
 * iris_wsqueue_local_push:
 * @queue: An #IrisWSQueue
 * @data: a pointer to data
 *
 * Pushes an item onto the queue.  This should only be called from the thread
 * that owns the #IrisWSQueue as it is not safe to call from other threads.
 * Research shows that only work items yielded from the owning thread should
 * land directly into this queue.
 */
void
iris_wsqueue_local_push (IrisWSQueue *queue,
                         gpointer     data)
{
	gpointer *old_items;
	gpointer *new_items;
	gint      tail;
	gint      head;
	gint      count;

	g_return_if_fail (queue != NULL);

	tail = queue->tail_idx;

	if (tail < (queue->head_idx + queue->mask)) {
		/* local push fast path */
		queue->items [tail & queue->mask] = data;
		g_atomic_int_set (&queue->tail_idx, tail + 1);
	}
	else {
		/* slow path, must resize the array */
		g_mutex_lock (queue->mutex);

		head = queue->head_idx;
		count = queue->tail_idx - queue->head_idx;
		old_items = queue->items;

		if (count >= queue->mask) {
			/* double the array size */
			new_items = g_malloc0 (sizeof (gpointer) * (queue->length << 1));

			/* copy the existing items over */
			memcpy (new_items,
			        old_items,
			        queue->length * sizeof (gpointer));

			/* assign the new array */
			queue->items = new_items;
			queue->head_idx = 0;
			queue->tail_idx = tail = count;
			queue->mask = (queue->mask << 1) | 1;
			queue->length = queue->length << 1;

			/* FIXME: Free old_items safely (can't really be done)
			 *   or save the buffer to a list for periodic GC.
			 *   However, as Shapor mentioned, leaking this will
			 *   only account for a total of 2x the largest growth
			 *   which may not be a problem anyway.
			 */
			/* g_free (old_items); */
		}

		queue->items [tail & queue->mask] = data;
		queue->tail_idx = tail + 1;

		g_mutex_unlock (queue->mutex);
	}
}

/**
 * iris_wsqueue_local_pop:
 * @queue: An #IrisWSQueue
 *
 * Performs a local pop on the queue.  This should only be called by the
 * thread that owns the #IrisWSQueue.
 *
 * Return value: A pointer or %NULL if the queue is empty.
 */
gpointer
iris_wsqueue_local_pop (IrisWSQueue *queue)
{
	gpointer result = NULL;
	gint     tail;

	g_return_val_if_fail (queue != NULL, NULL);

	tail = queue->tail_idx;
	if (queue->head_idx >= tail)
		return NULL;

	tail -= 1;
	g_atomic_int_set (&queue->tail_idx, tail);

	if (queue->head_idx <= tail) {
		result = queue->items [tail & queue->mask];
	}
	else {
		g_mutex_lock (queue->mutex);

		if (queue->head_idx <= tail) {
			/* item is still available */
			result = queue->items [tail & queue->mask];
		}
		else {
			/* we lost the race, restore the tail */
			queue->tail_idx = tail + 1;
			result = NULL;
		}

		g_mutex_unlock (queue->mutex);
	}

	return result;
}

/**
 * iris_wsqueue_try_steal:
 * @queue: An #IrisWSQueue
 * @timeout: timeout in millseconds
 *
 * Tries to steal an item from the #IrisWSQueue within the timeout
 * specified.
 *
 * Return value: A gpointer or %NULL if no items were available.
 */
gpointer
iris_wsqueue_try_steal (IrisWSQueue *queue,
                        guint        timeout)
{
	GTimeVal tv     = {0,0};
	gboolean taken  = FALSE;
	gpointer result = NULL;
	gint     head;

	g_return_val_if_fail (queue != NULL, NULL);

	g_get_current_time (&tv);
	g_time_val_add (&tv, (G_USEC_PER_SEC / 1000) * timeout);

	/* GMutex does not export timedlock, so for now we will use
	 * the non-portable pthread call directly.
	 */
	taken = pthread_mutex_timedlock (
			(pthread_mutex_t*)queue->mutex,
			(struct timespec*)&tv);

	head = queue->head_idx;
	g_atomic_int_set (&queue->head_idx, head + 1);

	if (head < queue->tail_idx) {
		result = queue->items [head & queue->mask];
	}
	else {
		queue->head_idx = head;
	}

	if (taken)
		g_mutex_unlock (queue->mutex);

	return result;
}

GType
iris_wsqueue_get_type (void)
{
	static GType wsqueue_type = 0;
	if (G_UNLIKELY (!wsqueue_type))
		wsqueue_type = g_boxed_type_register_static (
				"IrisWSQueue",
				(GBoxedCopyFunc)iris_queue_ref,
				(GBoxedFreeFunc)iris_queue_unref);
	return wsqueue_type;
}
