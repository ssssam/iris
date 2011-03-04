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
 * @see_also: #IrisLFQueue, #IrisWSQueue
 *
 * #IrisQueue is a queue abstraction for concurrent queues.  The default
 * implementation wraps #GAsyncQueue which is a lock-based queue.
 *
 * A useful feature of #IrisQueue is the 'closing' of queues. This has two
 * possible uses. Firstly, the owner of the queue can call iris_queue_close()
 * to signal to all listeners that there will be no more items. Alternatively,
 * if the queue has only one listener, that listener can use
 * iris_queue_timed_pop_or_close() or iris_queue_try_pop_or_close() to signal
 * to the source of the data that the items will no longer be processed.
 * The advantage of this method of signalling is that it implicitly prevents
 * race conditions from occurring without adding an extra mutex.
 *
 * Callers should check the return value of iris_queue_push() to see if an
 * item posted successfully.
 *
 * <refsect2 id="examples">
 * <title>Examples</title>
 * <para>
 * A good example is found in Iris' scheduling: a transient #IrisThread will
 * process work out of a queue until the queue is empty and then stop it. The
 * thread must notify the scheduler that the queue will no longer be processed
 * before the scheduler can enqueue more items. Using
 * iris_queue_try_pop_or_close() to get items from the queue is a quick and
 * neat solution.
 *
 * Exclusive threads use closing in the other direction: when a scheduler is
 * destroyed, it notifies its own threads to stop processing its work queues
 * using iris_queue_close(). Again, this is neater than sending a 'dummy' work
 * item to signify closure or setting up a separate message queue for just
 * one message.
 * </para>
 * </refsect2>
 */

G_DEFINE_TYPE (IrisQueue, iris_queue, G_TYPE_OBJECT)

static gboolean iris_queue_real_push               (IrisQueue *queue,
                                                    gpointer   data);
static gpointer iris_queue_real_pop                (IrisQueue *queue);
static gpointer iris_queue_real_try_pop            (IrisQueue *queue);
static gpointer iris_queue_real_timed_pop          (IrisQueue *queue,
                                                    GTimeVal  *timeout);
static gpointer iris_queue_real_try_pop_or_close   (IrisQueue *queue);
static gpointer iris_queue_real_timed_pop_or_close (IrisQueue *queue,
                                                    GTimeVal  *timeout);
static void     iris_queue_real_close              (IrisQueue *queue);
static guint    iris_queue_real_get_length         (IrisQueue *queue);
static gboolean iris_queue_real_is_closed          (IrisQueue *queue);


/* IrisQueue currently involves some hacks around the GAsyncQueue codebase,
 * mainly to implement closing. I don't think that's a problem, the code hasn't
 * changed too much since 2000.
 */
struct _GAsyncQueue
{
  GMutex *mutex;
  GCond *cond;
  GQueue queue;
  GDestroyNotify item_free_func;
  guint waiting_threads;
  gint32 ref_count;
};

/* Value pushed by iris_queue_close() to wake up pop listeners. Note that it
 * isn't a problem that this value might conflict with legitimate items. Pop
 * functions will only treat this value as special if it is the last item in a
 * closed queue, and since closing the queue and pushing the token happens
 * atomically (and always happens) there is no danger of confusion.
 */
#define CLOSE_TOKEN (gpointer)0x12345678

static void
iris_queue_finalize (GObject *object)
{
	IrisQueue *queue = IRIS_QUEUE (object);

	if (queue->priv->q != NULL)
		g_async_queue_unref (queue->priv->q);

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
	klass->try_pop_or_close = iris_queue_real_try_pop_or_close;
	klass->timed_pop_or_close = iris_queue_real_timed_pop_or_close;
	klass->close = iris_queue_real_close;
	klass->get_length = iris_queue_real_get_length;
	klass->is_closed = iris_queue_real_is_closed;
}

static void
iris_queue_init (IrisQueue *queue)
{
	queue->priv = G_TYPE_INSTANCE_GET_PRIVATE (queue, IRIS_TYPE_QUEUE, IrisQueuePrivate);

	/* only create GAsyncQueue if needed */
	if (G_TYPE_FROM_INSTANCE (queue) == IRIS_TYPE_QUEUE)
		queue->priv->q = g_async_queue_new ();
	else
		queue->priv->q = NULL;

	queue->priv->open = TRUE;
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
 * Pushes a non-%NULL pointer onto the queue, if it is not closed.
 *
 * Return value: %TRUE if @data was pushed successfully, %FALSE if @queue is
 *               closed.
 */
gboolean
iris_queue_push (IrisQueue *queue,
                 gpointer   data)
{
	return IRIS_QUEUE_GET_CLASS (queue)->push (queue, data);
}

/**
 * iris_queue_pop:
 * @queue: An #IrisQueue
 *
 * Pops an item off the queue.  It is up to the queue implementation to
 * determine if this method should block.  The default implementation of
 * #IrisQueue blocks until an item is available or the queue is closed.
 *
 * iris_queue_pop() will return items from a closed queue until the queue is
 * empty, at which point it will return %NULL when called.
 *
 * Return value: the next item off the queue, or %NULL if the queue became
 *               closed or there was an error.
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
 * is returned. If @queue is closed, behaviour is the same.
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
 * Pops an item off the queue, or waits for @timeout to elapse for one to be
 * pushed. If @queue remains empty, %NULL is returned when @timeout has passed
 * if @queue becomes closed.
 *
 * Return value: the next item off the queue, or %NULL if @timeout has passed
 *               or the queue became closed.
 */
gpointer
iris_queue_timed_pop (IrisQueue *queue,
                      GTimeVal  *timeout)
{
	return IRIS_QUEUE_GET_CLASS (queue)->timed_pop (queue, timeout);
}

/**
 * iris_queue_try_pop_or_close:
 * @queue: An #IrisQueue
 *
 * Tries to pop an item off the queue.  If no item is available %NULL
 * is returned and the queue is closed, so that no more items may be
 * enqueued.
 *
 * Return value: the next item off the queue, or %NULL if none was available
 *               or the queue became closed.
 */
gpointer
iris_queue_try_pop_or_close (IrisQueue *queue)
{
	return IRIS_QUEUE_GET_CLASS (queue)->try_pop_or_close (queue);
}

/**
 * iris_queue_timed_pop_or_close:
 * @queue: An #IrisQueue
 * @timeout: the absolute timeout for pop
 *
 * Pops an item off the queue or returns %NULL when @timeout has passed. If
 * %NULL is returned the queue will be closed so that no more items may be
 * posted.
 *
 * Return value: the next item off the queue, or %NULL if @timeout has passed
 *               or @queue becomes closed.
 */
gpointer
iris_queue_timed_pop_or_close (IrisQueue *queue,
                               GTimeVal  *timeout)
{
	return IRIS_QUEUE_GET_CLASS (queue)->timed_pop_or_close (queue, timeout);
}

/**
 * iris_queue_close:
 * @queue: An #IrisQueue
 *
 * Closes @queue. If the queue is empty, any thread that is waiting on
 * iris_queue_pop() will receive %NULL immediately. See the introduction for
 * more information on queue closing.
 */
void
iris_queue_close (IrisQueue *queue)
{
	IRIS_QUEUE_GET_CLASS (queue)->close (queue);
}

/**
 * iris_queue_get_length:
 * @queue: An #IrisQueue
 *
 * Retrieves the current length of the queue.
 *
 * Return value: the length of the queue
 */
guint
iris_queue_get_length (IrisQueue *queue)
{
	return IRIS_QUEUE_GET_CLASS (queue)->get_length (queue);
}

/**
 * iris_queue_is_closed:
 * @queue: An #IrisQueue
 *
 * Returns whether the @queue has been closed.
 *
 * <note>
 * The following code is NOT thread-safe:
 *  |[
 *     if (iris_queue_is_closed (queue))
 *       return FALSE;
 *     else
 *       iris_queue_push (queue, item);
 *  ]|
 * The queue may become closed between the call to iris_queue_is_closed() and
 * iris_queue_push(); the correct method is simply to call iris_queue_push()
 * and check the return value.
 * </note>
 *
 * Return value: %TRUE if the queue is no longer accepting entries.
 */
gboolean
iris_queue_is_closed (IrisQueue *queue)
{
	return IRIS_QUEUE_GET_CLASS (queue)->is_closed (queue);
}


/**************************************************************************
 *                     IrisQueue default implementation                   *
 *************************************************************************/


static void
close_ul (IrisQueue *queue)
{
	gint         i;

	g_atomic_int_set (&queue->priv->open, FALSE);

	/* Send the close token to any threads currently blocking on the queue. */
	for (i=0; i < queue->priv->q->waiting_threads; i++)
		g_async_queue_push_unlocked (queue->priv->q, CLOSE_TOKEN);

	queue->priv->close_token_count = queue->priv->q->waiting_threads;
}

/* Swallow CLOSE_TOKEN items */
static gpointer
handle_close_token_ul (IrisQueue *queue,
                       gpointer   item)
{
	gint remaining_items;

	if (item == NULL)
		return NULL;

	/* Filter close tokens. They must be the last items in the queue, so we can
	 * avoid filtering actual queue items that happen to be the same value.
	 */
	remaining_items = g_async_queue_length_unlocked (queue->priv->q) + 
	                  queue->priv->q->waiting_threads;

	g_warn_if_fail (remaining_items >= queue->priv->close_token_count -1);

	if ((queue->priv->close_token_count > 0) &&
	     remaining_items == queue->priv->close_token_count -1) {
		g_warn_if_fail (item == CLOSE_TOKEN);

		queue->priv->close_token_count --;
		return NULL;
	} else
		return item;
}


static gboolean
iris_queue_real_push (IrisQueue *queue,
                      gpointer   data)
{
	gboolean is_open;

	g_return_val_if_fail (data != NULL, FALSE);

	g_async_queue_lock (queue->priv->q);

	is_open = g_atomic_int_get (&queue->priv->open);

	if (G_LIKELY (is_open))
		g_async_queue_push_unlocked (queue->priv->q, data);

	g_async_queue_unlock (queue->priv->q);

	return is_open;
}

static gpointer
iris_queue_real_pop (IrisQueue *queue)
{
	gpointer item;

	g_async_queue_lock (queue->priv->q);

	if (g_atomic_int_get (&queue->priv->open) == FALSE &&
	    g_async_queue_length_unlocked (queue->priv->q) <= 0) {
		g_async_queue_unlock (queue->priv->q);
		return NULL;
	}

	item = g_async_queue_pop_unlocked (queue->priv->q);

	if (g_atomic_int_get (&queue->priv->open) == FALSE)
		item = handle_close_token_ul (queue, item);
	g_async_queue_unlock (queue->priv->q);

	return item;
}

static gpointer
iris_queue_real_try_pop (IrisQueue *queue)
{
	gpointer item;

	g_async_queue_lock (queue->priv->q);
	item = g_async_queue_try_pop_unlocked (queue->priv->q);

	if (g_atomic_int_get (&queue->priv->open) == FALSE)
		item = handle_close_token_ul (queue, item);
	g_async_queue_unlock (queue->priv->q);

	return item;
}

static gpointer
iris_queue_real_timed_pop (IrisQueue *queue,
                           GTimeVal  *timeout)
{
	gpointer item;

	g_async_queue_lock (queue->priv->q);
	if (g_atomic_int_get (&queue->priv->open) == FALSE &&
	    g_async_queue_length_unlocked (queue->priv->q) <= 0) {
		g_async_queue_unlock (queue->priv->q);
		return NULL;
	}

	item = g_async_queue_timed_pop_unlocked (queue->priv->q, timeout);

	if (g_atomic_int_get (&queue->priv->open) == FALSE)
		item = handle_close_token_ul (queue, item);
	g_async_queue_unlock (queue->priv->q);

	return item;
}

static gpointer
iris_queue_real_try_pop_or_close (IrisQueue *queue)
{
	gpointer item;

	g_async_queue_lock (queue->priv->q);
	item = g_async_queue_try_pop_unlocked (queue->priv->q);

	if (g_atomic_int_get (&queue->priv->open) == FALSE)
		item = handle_close_token_ul (queue, item);
	else if (item == NULL)
		close_ul (queue);
	g_async_queue_unlock (queue->priv->q);

	return item;
}

static gpointer
iris_queue_real_timed_pop_or_close (IrisQueue *queue,
                                    GTimeVal  *timeout)
{
	gpointer item;

	g_async_queue_lock (queue->priv->q);
	item = g_async_queue_timed_pop_unlocked (queue->priv->q, timeout);

	if (g_atomic_int_get (&queue->priv->open) == FALSE)
		item = handle_close_token_ul (queue, item);
	else if (item == NULL)
		close_ul (queue);
	g_async_queue_unlock (queue->priv->q);

	return item;
}

static void
iris_queue_real_close (IrisQueue *queue)
{
	g_async_queue_lock (queue->priv->q);
	close_ul (queue);
	g_async_queue_unlock (queue->priv->q);
}

static guint
iris_queue_real_get_length (IrisQueue *queue)
{
	return g_async_queue_length (queue->priv->q);
}

static gboolean
iris_queue_real_is_closed (IrisQueue *queue)
{
	return ! g_atomic_int_get (&queue->priv->open);
}
