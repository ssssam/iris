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

/**
 * iris_queue_new:
 *
 * Creates a new instance of #IrisQueue, a concurrent, lock-free queue.
 *
 * Return value: The newly created #IrisQueue instance.
 */
IrisQueue*
iris_queue_new (void)
{
	IrisQueue *queue;

	queue = g_slice_new0 (IrisQueue);
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
 * Frees the memory associated with an #IrisQueue. Obviously, this method
 * is not thread-safe, as you should not be accessing the queue when
 * freeing.
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
 * Enqueues a new pointer into the queue atomically.
 */
void
iris_queue_enqueue (IrisQueue *queue,
                    gpointer   data)
{
	IrisLink *old_tail = NULL;
	IrisLink *old_next = NULL;
	IrisLink *link     = NULL;
	gboolean  success  = FALSE;

	g_return_if_fail (queue != NULL);

	link = iris_free_list_get (queue->free_list);
	link->data = data;

	while (!success) {
		old_tail = queue->tail;
		old_next = old_tail->next;

		if (queue->tail == old_tail) {
			if (!old_next) {
				success = g_atomic_pointer_compare_and_exchange (
						(gpointer*)&queue->tail->next,
						NULL, link);
			}
		}
		else {
			g_atomic_pointer_compare_and_exchange (
					(gpointer*)&queue->tail,
					old_tail, old_next);
		}
	}

	g_atomic_pointer_compare_and_exchange (
			(gpointer*)&queue->tail,
			old_tail, link);
	g_atomic_int_inc ((gint*)&queue->length);
}

/**
 * iris_queue_dequeue:
 * @queue: An #IrisQueue
 *
 * Dequeues the next item from the queue atomically, or %NULL
 *
 * Return value: the next item from the queue or %NULL
 */
gpointer
iris_queue_dequeue (IrisQueue *queue)
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
		old_head_next = old_head->next;

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
				result = old_head_next->data;
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

/**
 * iris_queue_get_length:
 * @queue: An #IrisQueue
 *
 * Retreives the length of the queue.
 *
 * Return value: the length of the queue.
 */
guint
iris_queue_get_length (IrisQueue *queue)
{
	g_return_val_if_fail (queue != NULL, 0);
	return g_atomic_int_get (&queue->length);
}
