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

#include "gstamppointer.h"
#include "iris-lfqueue.h"
#include "iris-lfqueue-private.h"
#include "iris-util.h"

/**
 * SECTION:iris-lfqueue
 * @title: IrisLFQueue
 * @short_description: A lock-free queue
 *
 * #IrisLFQueue is a lock-free queue.  If you use iris_queue_try_pop()
 * to retrieve items from the queue it is also a non-blocking queue.
 * Pushing new items onto the queue with iris_queue_push() is also
 * non-blocking.
 *
 * Keep in mind that lock-free is not always the fastest implementation
 * for all problem sets.
 *
 * <warning><para>
 * #IrisLFQueue is experimental code and may not run correctly. Do
 * not use it in production!
 * </para></warning>
 */

static guint    iris_lfqueue_real_get_length (IrisQueue *queue);
static gpointer iris_lfqueue_real_pop        (IrisQueue *queue);
static gpointer iris_lfqueue_real_timed_pop  (IrisQueue *queue,
                                              GTimeVal  *timeout);
static gpointer iris_lfqueue_real_try_pop    (IrisQueue *queue);
static gboolean iris_lfqueue_real_push       (IrisQueue *queue,
                                              gpointer   data);

G_DEFINE_TYPE (IrisLFQueue, iris_lfqueue, IRIS_TYPE_QUEUE)

static void
iris_lfqueue_finalize (GObject *object)
{
	IrisLFQueuePrivate *priv;
	IrisLink           *link, *tmp;

	priv = IRIS_LFQUEUE (object)->priv;

	link = priv->head;
	priv->tail = NULL;

	while (link) {
		tmp = G_STAMP_POINTER_GET_LINK (link)->next;
		if (link)
			g_slice_free (IrisLink, G_STAMP_POINTER_GET_POINTER (link));
		link = tmp;
	}

	iris_free_list_free (priv->free_list);

	G_OBJECT_CLASS (iris_lfqueue_parent_class)->finalize (object);
}

static void
iris_lfqueue_class_init (IrisLFQueueClass *klass)
{
	GObjectClass   *object_class;
	IrisQueueClass *queue_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = iris_lfqueue_finalize;
	g_type_class_add_private (object_class, sizeof (IrisLFQueuePrivate));

	queue_class = IRIS_QUEUE_CLASS (klass);
	queue_class->push = iris_lfqueue_real_push;
	queue_class->pop = iris_lfqueue_real_pop;
	queue_class->try_pop = iris_lfqueue_real_try_pop;
	queue_class->timed_pop = iris_lfqueue_real_timed_pop;
	queue_class->get_length = iris_lfqueue_real_get_length;
}

static void
iris_lfqueue_init (IrisLFQueue *queue)
{
	queue->priv = G_TYPE_INSTANCE_GET_PRIVATE (queue,
	                                           IRIS_TYPE_LFQUEUE,
	                                           IrisLFQueuePrivate);

	queue->priv->head = g_slice_new0 (IrisLink);
	queue->priv->tail = queue->priv->head;
	queue->priv->free_list = iris_free_list_new ();
}

/**
 * iris_lfqueue_new:
 *
 * Creates a new instance of #IrisLFQueue, a lock-free queue.
 *
 * Return value: the newly created #IrisLFQueue instance
 */
IrisQueue*
iris_lfqueue_new ()
{
	return g_object_new (IRIS_TYPE_LFQUEUE, NULL);
}

static gboolean
iris_lfqueue_real_push (IrisQueue *queue,
                        gpointer   data)
{
	IrisLFQueuePrivate *priv;
	IrisLink           *old_tail;
	IrisLink           *old_next;
	IrisLink           *link;
	gboolean            success = FALSE;

	g_return_val_if_fail (queue != NULL, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	priv = IRIS_LFQUEUE (queue)->priv;
	link = G_STAMP_POINTER_INCREMENT (iris_free_list_get (priv->free_list));
	G_STAMP_POINTER_GET_LINK (link)->data = data;

	while (!success) {
		old_tail = priv->tail;
		old_next = G_STAMP_POINTER_GET_LINK (old_tail)->next;

		if (priv->tail == old_tail) {
			if (!old_next) {
				success = g_atomic_pointer_compare_and_exchange (
						(gpointer*)&G_STAMP_POINTER_GET_LINK (priv->tail)->next,
						NULL, link);
			}
		}
		else {
			g_atomic_pointer_compare_and_exchange (
					(gpointer*)&priv->tail, old_tail, old_next);
		}
	}

	g_atomic_pointer_compare_and_exchange ((gpointer*)&priv->tail, old_tail, link);
	g_atomic_int_inc ((gint*)&priv->length);

	return TRUE;
}

static gpointer
iris_lfqueue_real_try_pop (IrisQueue *queue)
{
	IrisLFQueuePrivate *priv;
	IrisLink           *old_head;
	IrisLink           *old_tail;
	IrisLink           *old_head_next;
	gboolean            success       = FALSE;
	gpointer            result        = NULL;

	g_return_val_if_fail (queue != NULL, NULL);

	priv = IRIS_LFQUEUE (queue)->priv;

	while (!success) {
		old_head = priv->head;
		old_tail = priv->tail;
		old_head_next = G_STAMP_POINTER_GET_LINK (old_head)->next;

		if (old_head == priv->head) {
			if (old_head == old_tail) {
				if (!old_head_next)
					return NULL;

				g_atomic_pointer_compare_and_exchange (
						(gpointer*)&priv->tail,
						old_tail,
						old_head_next);
			}
			else {
				result = G_STAMP_POINTER_GET_LINK (old_head_next)->data;
				success = g_atomic_pointer_compare_and_exchange (
						(gpointer*)&priv->head,
						old_head,
						old_head_next);
			}
		}
	}

	iris_free_list_put (priv->free_list, old_head);
	(void)g_atomic_int_dec_and_test ((gint*)&priv->length);

	return result;
}

static gpointer
iris_lfqueue_real_timed_pop (IrisQueue *queue,
                             GTimeVal  *timeout)
{
	gpointer result;
	gint     spin_count = 0;
	glong    usec;

	g_return_val_if_fail (queue != NULL, NULL);

retry:
	if (!(result = iris_lfqueue_real_try_pop (queue))) {
		/* spin a few times retrying */
		if (spin_count < 5) {
			spin_count++;
			goto retry;
		}

		/* make sure the timeout hasn't passed */
		usec = g_time_val_usec_until (timeout);
		if (usec <= 0)
			return NULL;

		/* sleep 10 milliseconds */
		g_usleep (10000);
		goto retry;
	}

	return result;
}

static gpointer
iris_lfqueue_real_pop (IrisQueue *queue)
{
	/* since this method must block, we will retry things periodically
	 * since we cannot add a lock/condition wait.  This should *not*
	 * be used on power saving devices such as embedded platforms.
	 */

	gpointer result  = NULL;
	GTimeVal timeout = {0,0};

	if ((result = iris_lfqueue_real_try_pop (queue)) != NULL)
		return result;

	do {
		g_get_current_time (&timeout);
		g_time_val_add (&timeout, G_USEC_PER_SEC / 5); /* 200ms */
		result = iris_lfqueue_real_timed_pop (queue, &timeout);
	} while (!result);

	return result;
}

static guint
iris_lfqueue_real_get_length (IrisQueue *queue)
{
	return IRIS_LFQUEUE(queue)->priv->length;
}
