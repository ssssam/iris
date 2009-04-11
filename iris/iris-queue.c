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
 * @short_description: A concurrent queue data-structure
 *
 * #IrisQueue is a lock-free data structure that fits well into highly
 * concurrent scenarios.  You can create your own queue implementation
 * by overriding the vtable value in the struct. By adhering to the
 * #IrisQueue interface for the vtable, you can have alternate queuing
 * logic for schedulers and the thread workers will still know how to
 * access your work items.
 */

static void
iris_queue_enqueue_real (IrisQueue *queue,
                         gpointer   data)
{
	IrisLink *old_tail = NULL;
	IrisLink *old_next = NULL;
	IrisLink *link     = NULL;
	gboolean  success  = FALSE;

	g_return_if_fail (queue != NULL);

	link = iris_free_list_get (queue->free_list);

	link = G_STAMP_POINTER_INCREMENT (link);
	G_STAMP_POINTER_GET_LINK (link)->data = data;

	while (!success) {
		old_tail = queue->tail;
		old_next = G_STAMP_POINTER_GET_LINK (old_tail)->next;

		if (queue->tail == old_tail) {
			if (!old_next) {
				success = g_atomic_pointer_compare_and_exchange (
						(gpointer*)&G_STAMP_POINTER_GET_LINK (queue->tail)->next,
						NULL,
						link);
			}
		}
		else {
			g_atomic_pointer_compare_and_exchange (
					(gpointer*)&queue->tail,
					old_tail,
					old_next);
		}
	}

	g_atomic_pointer_compare_and_exchange ((gpointer*)&queue->tail,
	                                       old_tail,
	                                       link);

	g_atomic_int_inc ((gint*)&queue->length);
}

static gpointer
iris_queue_dequeue_real (IrisQueue *queue)
{
	IrisLink *old_head      = NULL;
	IrisLink *old_tail      = NULL;
	IrisLink *old_head_next = NULL;
	gboolean  success       = FALSE;
	gpointer  result        = NULL;

	g_return_val_if_fail (queue != NULL, NULL);

	while (!success) {
		old_head = queue->head;
		old_tail = queue->tail;
		old_head_next = G_STAMP_POINTER_GET_LINK (old_head)->next;

		if (old_head == queue->head) {
			if (old_head == old_tail) {
				if (!old_head_next)
					return NULL;

				g_atomic_pointer_compare_and_exchange (
						(gpointer*)&queue->tail,
						old_tail,
						old_head_next);
			}
			else {
				result = G_STAMP_POINTER_GET_LINK (old_head_next)->data;
				success = g_atomic_pointer_compare_and_exchange (
						(gpointer*)&queue->head,
						old_head,
						old_head_next);
			}
		}
	}

	iris_free_list_put (queue->free_list, old_head);
	if (g_atomic_int_dec_and_test ((gint*)&queue->length)) {}

	return result;
}

static guint
iris_queue_get_length_real (IrisQueue *queue)
{
	return queue->length;
}

static IrisQueueVTable queue_vtable = {
	iris_queue_enqueue_real,
	iris_queue_dequeue_real,
	iris_queue_get_length_real,
};

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
	queue->head = g_slice_new0 (IrisLink);
	queue->tail = queue->head;
	queue->free_list = iris_free_list_new ();
	queue->length = 0;

	return queue;
}

/**
 * iris_queue_free:
 * @queue: An #IrisQueue
 *
 * Frees the memory associated with an #IrisQueue.
 *
 * You should not be accessing this queue concurrently when freeing it.
 */
void
iris_queue_free (IrisQueue *queue)
{
	IrisLink *link, *tmp;

	g_return_if_fail (queue != NULL);

	link = queue->head;
	queue->tail = NULL;

	while (link) {
		tmp = link->next;
		if (tmp)
			g_slice_free (IrisLink, tmp);
		link = tmp;
	}

	iris_free_list_free (queue->free_list);
	g_slice_free (IrisQueue, queue);
}

/**
 * iris_queue_enqueue:
 * @queue: An #IrisQueue
 * @data: a gpointer
 *
 * Enqueues a new pointer into the queue. The default implementation does
 * this atomically and lock-free.
 */
void
iris_queue_enqueue (IrisQueue *queue,
                    gpointer   data)
{
	g_return_if_fail (queue != NULL);
	queue->vtable->enqueue (queue, data);
}

/**
 * iris_queue_dequeue:
 * @queue: An #IrisQueue
 *
 * Dequeues the next item from the queue. The default implementation does
 * this atomically and lock-free.
 *
 * Return value: the next item from the queue or %NULL
 */
gpointer
iris_queue_dequeue (IrisQueue *queue)
{
	g_return_val_if_fail (queue != NULL, NULL);
	return queue->vtable->dequeue (queue);
}

/**
 * iris_queue_get_length:
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
iris_queue_get_length (IrisQueue *queue)
{
	g_return_val_if_fail (queue != NULL, 0);
	return queue->vtable->get_length (queue);
}
