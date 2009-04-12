/* iris-queue.c
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
#include "gstamppointer.h"

/**
 * SECTION:iris-queue
 * @short_description: A concurrent queue.
 *
 * #IrisQueue is a queue abstraction for concurrent queues.  The default
 * implementation wraps #GAsyncQueue which is a lock-based queue.
 *
 * See also #IrisLFQueue and #IrisWSQueue.
 */

static void
iris_queue_push_real (IrisQueue *queue,
                      gpointer   data)
{
	return g_async_queue_push (queue->impl_queue, data);
}

static gpointer
iris_queue_pop_real (IrisQueue *queue)
{
	return g_async_queue_pop (queue->impl_queue);
}

static gpointer
iris_queue_try_pop_real (IrisQueue *queue)
{
	return g_async_queue_try_pop (queue->impl_queue);
}

static guint
iris_queue_length_real (IrisQueue *queue)
{
	return g_async_queue_length (queue->impl_queue);
}

static void
iris_queue_dispose_real (IrisQueue *queue)
{
	g_async_queue_unref (queue->impl_queue);
	queue->impl_queue = NULL;
	g_slice_free (IrisQueue, queue);
}

static IrisQueueVTable queue_vtable = {
	iris_queue_push_real,
	iris_queue_pop_real,
	iris_queue_try_pop_real,
	NULL,
	iris_queue_length_real,
	iris_queue_dispose_real,
};

static void
iris_queue_dispose (IrisQueue *queue)
{
	if (G_LIKELY (queue->vtable->dispose))
		queue->vtable->dispose (queue);
}

/**
 * iris_queue_new:
 *
 * Creates a new instance of #IrisQueue.
 *
 * The default implementation of #IrisQueue is a lock-free queue that works
 * well under highly concurrent scenarios.
 *
 * Return value: The newly created #IrisQueue instance.
 */
IrisQueue*
iris_queue_new (void)
{
	IrisQueue *queue;

	queue = g_slice_new0 (IrisQueue);
	queue->vtable = &queue_vtable;
	queue->ref_count = 1;
	queue->impl_queue = g_async_queue_new ();

	return queue;
}

/**
 * iris_queue_ref:
 * @queue: An #IrisQueue
 *
 * Increases the reference count of @queue atomically by one.
 *
 * Return value: the @queue pointer.
 */
IrisQueue*
iris_queue_ref (IrisQueue *queue)
{
	g_return_val_if_fail (queue != NULL, NULL);
	g_return_val_if_fail (queue->ref_count > 0, NULL);

	g_atomic_int_inc (&queue->ref_count);

	return queue;
}

/**
 * iris_queue_unref:
 * @queue: An #IrisQueue
 *
 * Atomically decreases the reference count of @queue by one.  If the
 * reference count reaches zero, the queue is destroyed and all its
 * allocated resources are freed.
 */
void
iris_queue_unref (IrisQueue *queue)
{
	g_return_if_fail (queue != NULL);
	g_return_if_fail (queue->ref_count > 0);

	if (g_atomic_int_dec_and_test (&queue->ref_count))
		iris_queue_dispose (queue);
}

/**
 * iris_queue_push:
 * @queue: An #IrisQueue
 * @data: a gpointer
 *
 * Enqueues a new pointer into the queue. The default implementation does
 * this atomically and lock-free.
 */
void
iris_queue_push (IrisQueue *queue,
                 gpointer   data)
{
	g_return_if_fail (queue != NULL);
	queue->vtable->push (queue, data);
}

/**
 * iris_queue_pop:
 * @queue: An #IrisQueue
 *
 * Dequeues the next item from the queue. The default implementation does
 * this atomically and lock-free.
 *
 * Return value: the next item from the queue or %NULL
 */
gpointer
iris_queue_pop (IrisQueue *queue)
{
	g_return_val_if_fail (queue != NULL, NULL);
	return queue->vtable->pop (queue);
}

/**
 * iris_queue_try_pop:
 * @queue: An #IrisQueue
 *
 * Tries to pop an item off of the queue.  If no item is available, %NULL is
 * returned.
 *
 * Return value: An item from the queue or %NULL
 */
gpointer
iris_queue_try_pop (IrisQueue *queue)
{
	g_return_val_if_fail (queue != NULL, NULL);
	return queue->vtable->try_pop (queue);
}

/**
 * iris_queue_length:
 * @queue: An #IrisQueue
 *
 * Retreives the length of the queue.
 *
 * The default implementation does not use
 * a fence since the length of a concurrent queue may not be the same between
 * a read and a write anyway. This means that updates from other threads may
 * not have propogated the cache lines to the host cpu (but in most cases,
 * this is probably fine).
 *
 * Return value: the length of the queue.
 */
guint
iris_queue_length (IrisQueue *queue)
{
	g_return_val_if_fail (queue != NULL, 0);
	return queue->vtable->length (queue);
}
