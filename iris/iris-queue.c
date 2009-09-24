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
#include "iris-queue-private.h"

/**
 * SECTION:iris-queue
 * @title: IrisQueue
 * @short_description: Thread-safe queues
 *
 * #IrisQueue is a queue abstraction for concurrent queues.  The default
 * implementation wraps #GAsyncQueue which is a lock-based queue.
 *
 * See also #IrisLFQueue and #IrisWSQueue
 */

G_DEFINE_TYPE (IrisQueue, iris_queue, G_TYPE_OBJECT)

static void     iris_queue_real_push      (IrisQueue *queue,
                                           gpointer   data);
static gpointer iris_queue_real_pop       (IrisQueue *queue);
static gpointer iris_queue_real_try_pop   (IrisQueue *queue);
static gpointer iris_queue_real_timed_pop (IrisQueue *queue,
                                           GTimeVal  *timeout);
static guint    iris_queue_real_length    (IrisQueue *queue);

static void
iris_queue_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_queue_parent_class)->finalize (object);
}

static void
iris_queue_class_init (IrisQueueClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = iris_queue_finalize;
	g_type_class_add_private (object_class, sizeof (IrisQueuePrivate));

	klass->push = iris_queue_real_push;
	klass->pop = iris_queue_real_pop;
	klass->try_pop = iris_queue_real_try_pop;
	klass->timed_pop = iris_queue_real_timed_pop;
	klass->length = iris_queue_real_length;
}

static void
iris_queue_init (IrisQueue *queue)
{
	queue->priv = G_TYPE_INSTANCE_GET_PRIVATE (queue, IRIS_TYPE_QUEUE, IrisQueuePrivate);

	/* only create GAsyncQueue if needed */
	if (G_TYPE_FROM_INSTANCE (queue) == IRIS_TYPE_QUEUE)
		queue->priv->q = g_async_queue_new ();
}

/**
 * iris_queue_new:
 *
 * Creates a new instance of #IrisQueue.
 *
 * Return value: the newly created #IrisQueue.
 */
IrisQueue*
iris_queue_new ()
{
	return g_object_new (IRIS_TYPE_QUEUE, NULL);
}

/**
 * iris_queue_push:
 * @queue: An #IrisQueue
 * @data: a pointer to store that is not %NULL
 *
 * Pushes a non-%NULL pointer onto the queue.
 */
void
iris_queue_push (IrisQueue *queue,
                 gpointer   data)
{
	IRIS_QUEUE_GET_CLASS (queue)->push (queue, data);
}

/**
 * iris_queue_pop:
 * @queue: An #IrisQueue
 *
 * Pops an item off the queue.  It is up to the queue implementation to
 * determine if this method should block.  The default implementation of
 * #IrisQueue blocks until an item is available.
 *
 * Return value: the next item off the queue or %NULL if there was an error
 */
gpointer
iris_queue_pop (IrisQueue *queue)
{
	return IRIS_QUEUE_GET_CLASS (queue)->pop (queue);
}

/**
 * iris_queue_try_pop:
 * @queue: An #IrisQueue
 *
 * Tries to pop an item off the queue.  If no item is available %NULL
 * is returned.
 *
 * Return value: the next item off the queue or %NULL if none was available.
 */
gpointer
iris_queue_try_pop (IrisQueue *queue)
{
	return IRIS_QUEUE_GET_CLASS (queue)->try_pop (queue);
}

/**
 * iris_queue_timed_pop:
 * @queue: An #IrisQueue
 * @timeout: the absolute timeout for pop
 *
 * Pops an item off the queue or returns %NULL when @timeout has passed.
 *
 * Return value: the next item off the queue or %NULL if @timeout has passed.
 */
gpointer
iris_queue_timed_pop (IrisQueue *queue,
                      GTimeVal  *timeout)
{
	return IRIS_QUEUE_GET_CLASS (queue)->timed_pop (queue, timeout);
}

/**
 * iris_queue_length:
 * @queue: An #IrisQueue
 *
 * Retrieves the current length of the queue.
 *
 * Return value: the length of the queue
 */
guint
iris_queue_length (IrisQueue *queue)
{
	return IRIS_QUEUE_GET_CLASS (queue)->length (queue);
}

static void
iris_queue_real_push (IrisQueue *queue,
                      gpointer   data)
{
	g_async_queue_push (queue->priv->q, data);
}

static gpointer
iris_queue_real_pop (IrisQueue *queue)
{
	return g_async_queue_pop (queue->priv->q);
}

static gpointer
iris_queue_real_try_pop (IrisQueue *queue)
{
	return g_async_queue_try_pop (queue->priv->q);
}

static gpointer
iris_queue_real_timed_pop (IrisQueue *queue,
                           GTimeVal  *timeout)
{
	return g_async_queue_timed_pop (queue->priv->q, timeout);
}

static guint
iris_queue_real_length (IrisQueue *queue)
{
	return g_async_queue_length (queue->priv->q);
}
