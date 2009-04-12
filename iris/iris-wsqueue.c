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

#include "iris-wsqueue.h"
#include "gstamppointer.h"

#define WSQUEUE_DEFAULT_SIZE 32

/**
 * SECTION:iris-wsqueue
 * @short_description: A work-stealing queue
 *
 * #IrisWSQueue is a work-stealing version of #IrisQueue.  It requires access
 * to a global queue for a fallback when out of items.  If no item can be
 * retreived from the global queue, it will try to steal from its peers using
 * the #IrisRRobin of peer queues, which should also be #IrisQueue based.
 */

static void
iris_wsqueue_push_real (IrisQueue *queue,
                        gpointer   data)
{
	/* we only allow pushes from the local thread via local push, so
	 * this should never be hit. */
	g_assert_not_reached ();
}

static gpointer
iris_wsqueue_pop_real (IrisQueue *queue)
{
	IrisWSQueue *real_queue;
	g_return_val_if_fail (queue != NULL, NULL);
	real_queue = (IrisWSQueue*)queue;
	return NULL;
}

static gpointer
iris_wsqueue_try_pop_real (IrisQueue *queue)
{
	IrisWSQueue *real_queue;
	g_return_val_if_fail (queue != NULL, NULL);
	real_queue = (IrisWSQueue*)queue;
	return NULL;
}

static gpointer
iris_wsqueue_timed_pop_real (IrisQueue *queue, GTimeVal *timeout)
{
	IrisWSQueue *real_queue;
	g_return_val_if_fail (queue != NULL, NULL);
	real_queue = (IrisWSQueue*)queue;
	return NULL;
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
	queue->items = g_array_sized_new (FALSE, TRUE,
	                                  sizeof (gpointer),
	                                  WSQUEUE_DEFAULT_SIZE);
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
	GArray *new_items;
	GArray *old_items;
	gint    tail;
	gint    head;
	gint    count;

	g_return_if_fail (queue != NULL);

	tail = queue->tail_idx;

	if (tail < (queue->head_idx + queue->mask)) {
		/* local push fast path */
		g_array_insert_val (queue->items, tail & queue->mask, data);
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
			new_items = g_array_sized_new (FALSE, TRUE,
			                               sizeof (gpointer),
			                               old_items->len << 1);

			/* copy the existing items over */
			memcpy (new_items->data,
			        old_items->data,
			        old_items->len * sizeof (gpointer));

			/* assign the new array */
			queue->items = new_items;
			queue->head_idx = 0;
			queue->tail_idx = tail = count;
			queue->mask = (queue->mask << 1) | 1;
		}

		g_array_insert_val (queue->items, tail & queue->mask, data);
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
		result = g_array_index (queue->items, gpointer, tail & queue->mask);
	}
	else {
		g_mutex_lock (queue->mutex);

		if (queue->head_idx <= tail) {
			/* item is still available */
			result = g_array_index (queue->items, gpointer, tail & queue->mask);
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
		result = g_array_index (queue->items,
		                        gpointer,
		                        head & queue->mask);
	}
	else {
		queue->head_idx = head;
	}

	if (taken)
		g_mutex_unlock (queue->mutex);

	return result;
}
