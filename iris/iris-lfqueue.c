/* iris-lfqueue.c
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

#include "iris-lfqueue.h"
#include "iris-util.h"
#include "gstamppointer.h"

/**
 * SECTION:iris-lfqueue
 * @short_description: A lock-free queue data structure
 *
 * #IrisLFQueue is a lock-free version of #IrisQueue.  As such, it does not
 * provide blocking calls for iris_lfqueue_pop() or iris_lfqueue_timed_pop().
 * Both of these methods act like iris_lfqueue_try_pop().
 */

static void
iris_lfqueue_push_real (IrisQueue *queue,
                        gpointer   data)
{
	IrisLFQueue *real_queue = (IrisLFQueue*)queue;
	IrisLink    *old_tail   = NULL;
	IrisLink    *old_next   = NULL;
	IrisLink    *link       = NULL;
	gboolean     success    = FALSE;

	g_return_if_fail (real_queue != NULL);
	g_return_if_fail (data != NULL);

	link = iris_free_list_get (real_queue->free_list);

	link = G_STAMP_POINTER_INCREMENT (link);
	G_STAMP_POINTER_GET_LINK (link)->data = data;

	while (!success) {
		old_tail = real_queue->tail;
		old_next = G_STAMP_POINTER_GET_LINK (old_tail)->next;

		if (real_queue->tail == old_tail) {
			if (!old_next) {
				success = g_atomic_pointer_compare_and_exchange (
						(gpointer*)&G_STAMP_POINTER_GET_LINK (real_queue->tail)->next,
						NULL,
						link);
			}
		}
		else {
			g_atomic_pointer_compare_and_exchange (
					(gpointer*)&real_queue->tail,
					old_tail,
					old_next);
		}
	}

	g_atomic_pointer_compare_and_exchange ((gpointer*)&real_queue->tail,
	                                       old_tail,
	                                       link);

	g_atomic_int_inc ((gint*)&real_queue->length);
}

static gpointer
iris_lfqueue_try_pop_real (IrisQueue *queue)
{
	IrisLFQueue *real_queue    = (IrisLFQueue*)queue;
	IrisLink    *old_head      = NULL;
	IrisLink    *old_tail      = NULL;
	IrisLink    *old_head_next = NULL;
	gboolean     success       = FALSE;
	gpointer     result        = NULL;

	g_return_val_if_fail (real_queue != NULL, NULL);

	while (!success) {
		old_head = real_queue->head;
		old_tail = real_queue->tail;
		old_head_next = G_STAMP_POINTER_GET_LINK (old_head)->next;

		if (old_head == real_queue->head) {
			if (old_head == old_tail) {
				if (!old_head_next)
					return NULL;

				g_atomic_pointer_compare_and_exchange (
						(gpointer*)&real_queue->tail,
						old_tail,
						old_head_next);
			}
			else {
				result = G_STAMP_POINTER_GET_LINK (old_head_next)->data;
				success = g_atomic_pointer_compare_and_exchange (
						(gpointer*)&real_queue->head,
						old_head,
						old_head_next);
			}
		}
	}

	iris_free_list_put (real_queue->free_list, old_head);
	if (g_atomic_int_dec_and_test ((gint*)&real_queue->length)) {}

	return result;
}

static gpointer
iris_lfqueue_timed_pop_real (IrisQueue *queue,
                             GTimeVal  *timeout)
{
	gpointer result;
	gint     spin_count = 0;

retry:
	if (!(result = iris_lfqueue_try_pop_real (queue))) {
		/* spin a few times retrying */
		if (spin_count < 5) {
			spin_count++;
			goto retry;
		}

		/* This totally isn't ideal, but its what we deal with by
		 * doing a non-blocking lock-free queue.  We will just
		 * sleep until the timeout passes and check again.
		 */
		g_usleep (g_time_val_usec_until (timeout));
		goto retry;
	}

	return result;
}

static gpointer
iris_lfqueue_pop_real (IrisQueue *queue)
{
	/* since this method must block, we will retry things periodically
	 * since we cannot add a lock/condition wait.  This should *not*
	 * be used on power saving devices such as embedded platforms.
	 */

	gpointer result  = NULL;
	GTimeVal timeout = {0,0};

	do {
		g_get_current_time (&timeout);
		g_time_val_add (&timeout, G_USEC_PER_SEC / 5); /* 200ms */
		result = iris_lfqueue_timed_pop_real (queue, &timeout);
	} while (!result);

	return result;
}

static guint
iris_lfqueue_length_real (IrisQueue *queue)
{
	return ((IrisLFQueue*)queue)->length;
}

static void
iris_lfqueue_dispose_real (IrisQueue *queue)
{
	IrisLFQueue *real_queue;
	IrisLink    *link, *tmp;

	g_return_if_fail (queue != NULL);

	real_queue = (IrisLFQueue*)queue;
	link = real_queue->head;
	real_queue->tail = NULL;

	while (link) {
		tmp = G_STAMP_POINTER_GET_LINK (link)->next;
		if (link)
			g_slice_free (IrisLink, G_STAMP_POINTER_GET_POINTER (link));
		link = tmp;
	}

	iris_free_list_free (real_queue->free_list);
	g_slice_free (IrisLFQueue, real_queue);
}

static IrisQueueVTable lfqueue_vtable = {
	iris_lfqueue_push_real,
	iris_lfqueue_pop_real,
	iris_lfqueue_try_pop_real,
	iris_lfqueue_timed_pop_real,
	iris_lfqueue_length_real,
	iris_lfqueue_dispose_real,
};

/**
 * iris_lfqueue_new:
 *
 * Creates a new instance of #IrisLFQueue.
 *
 * Return value: The newly created #IrisLFQueue instance.
 */
IrisQueue*
iris_lfqueue_new (void)
{
	IrisLFQueue *queue;

	queue = g_slice_new0 (IrisLFQueue);

	queue->parent.vtable = &lfqueue_vtable;
	queue->parent.ref_count = 1;

	queue->head = g_slice_new0 (IrisLink);
	queue->tail = queue->head;
	queue->free_list = iris_free_list_new ();

	return (IrisQueue*)queue;
}
