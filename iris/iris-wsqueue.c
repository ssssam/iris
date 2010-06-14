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

/* This queue was implemented using information I learned from reading Joe Duffy's blog
 * entry on Work Stealing Queues as well as Nir Shavit's research on the subject.
 *
 * http://www.bluebytesoftware.com/blog/PermaLink,guid,1665653b-b5f3-49b4-8144-cfbc5e8c632b.aspx
 */

#include <string.h>
#include <sys/time.h>
#include <time.h>

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/errno.h>
#endif

#include "iris-wsqueue.h"
#include "iris-wsqueue-private.h"
#include "gstamppointer.h"

/**
 * SECTION:iris-wsqueue
 * @title: IrisWSQueue
 * @short_description: A work-stealing queue
 *
 * #IrisWSQueue is a work-stealing version of #IrisQueue.  It requires access
 * to a global queue for a fallback when out of items.  If no item can be
 * retreived from the global queue, it will try to steal from its peers using
 * the #IrisRRobin of peer queues, which should also be #IrisQueue based.
 */

static void     iris_wsqueue_real_push      (IrisQueue *queue,
                                             gpointer   data);
static gpointer iris_wsqueue_real_pop       (IrisQueue *queue);
static gpointer iris_wsqueue_real_try_pop   (IrisQueue *queue);
static gpointer iris_wsqueue_real_timed_pop (IrisQueue *queue,
                                             GTimeVal  *timeout);
static guint    iris_wsqueue_real_length    (IrisQueue *queue);

#define WSQUEUE_DEFAULT_SIZE 32

struct StealInfo
{
	IrisQueue *queue;
	gpointer   result;
};

G_DEFINE_TYPE (IrisWSQueue, iris_wsqueue, IRIS_TYPE_QUEUE)

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

static void
iris_wsqueue_finalize (GObject *object)
{
	IrisWSQueuePrivate *priv;

	priv = IRIS_WSQUEUE (object)->priv;

	g_object_unref (priv->global);
	iris_rrobin_unref (priv->rrobin);
	g_free (priv->items);

	G_OBJECT_CLASS (iris_wsqueue_parent_class)->finalize (object);
}

static void
iris_wsqueue_class_init (IrisWSQueueClass *klass)
{
	GObjectClass   *object_class;
	IrisQueueClass *queue_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = iris_wsqueue_finalize;
	g_type_class_add_private (object_class, sizeof (IrisWSQueuePrivate));

	queue_class = IRIS_QUEUE_CLASS (klass);
	queue_class->push = iris_wsqueue_real_push;
	queue_class->pop = iris_wsqueue_real_pop;
	queue_class->try_pop = iris_wsqueue_real_try_pop;
	queue_class->timed_pop = iris_wsqueue_real_timed_pop;
	queue_class->length = iris_wsqueue_real_length;
}

static void
iris_wsqueue_init (IrisWSQueue *queue)
{
	queue->priv = G_TYPE_INSTANCE_GET_PRIVATE (queue,
	                                           IRIS_TYPE_WSQUEUE,
	                                           IrisWSQueuePrivate);

	queue->priv->mask = WSQUEUE_DEFAULT_SIZE - 1;
	queue->priv->mutex = g_mutex_new ();
	queue->priv->length = WSQUEUE_DEFAULT_SIZE;
	queue->priv->items = g_malloc0 (sizeof (gpointer) * WSQUEUE_DEFAULT_SIZE);
	queue->priv->head_idx = 0;
	queue->priv->tail_idx = 0;
}

IrisQueue*
iris_wsqueue_new (IrisQueue  *global,
                  IrisRRobin *rrobin)
{
	IrisWSQueue *queue;

	g_return_val_if_fail (global != NULL, NULL);
	g_return_val_if_fail (rrobin != NULL, NULL);

	queue = g_object_new (IRIS_TYPE_WSQUEUE, NULL);
	queue->priv->global = g_object_ref (global);
	queue->priv->rrobin = iris_rrobin_ref (rrobin);

	return IRIS_QUEUE (queue);
}

static void
iris_wsqueue_real_push (IrisQueue *queue,
                        gpointer   data)
{
	g_warning ("iris_queue_push should not be called for IrisWSQueue");
}


static gboolean
iris_wsqueue_pop_cb (IrisRRobin *rrobin,
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
iris_wsqueue_real_pop (IrisQueue *queue)
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
		result = iris_queue_pop (IRIS_WSQUEUE (queue)->priv->global);
	}

	return result;
}

static gpointer
iris_wsqueue_real_try_pop (IrisQueue *queue)
{
	/*
	 * This code path is to only be hit by the thread that owns the Queue!
	 */
	return iris_wsqueue_real_pop (queue);
}

static gpointer
iris_wsqueue_real_timed_pop (IrisQueue *queue,
                             GTimeVal  *timeout)
{
	/*
	 * This code path is to only be hit by the thread that owns the Queue!
	 */

	IrisWSQueuePrivate *priv;
	struct StealInfo    steal;

	g_return_val_if_fail (queue != NULL, NULL);
	g_return_val_if_fail (timeout != NULL, NULL);

	priv = IRIS_WSQUEUE (queue)->priv;
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

	if (NULL != (steal.result = iris_wsqueue_local_pop (IRIS_WSQUEUE (queue))))
		return steal.result;
	else if (NULL != (steal.result = iris_queue_try_pop (priv->global)))
		return steal.result;

	iris_rrobin_foreach (priv->rrobin, iris_wsqueue_pop_cb, &steal);

	if (steal.result)
		return steal.result;

	/* Round Two */

	if (NULL != (steal.result = iris_queue_timed_pop (priv->global, timeout)))
		return steal.result;

	iris_rrobin_foreach (priv->rrobin, iris_wsqueue_pop_cb, &steal);

	return steal.result;
}

static guint
iris_wsqueue_real_length (IrisQueue *queue)
{
	IrisWSQueuePrivate *priv;

	g_return_val_if_fail (queue != NULL, 0);

	priv = IRIS_WSQUEUE (queue)->priv;

	return (priv->tail_idx - priv->head_idx);
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
	IrisWSQueuePrivate *priv;
	gpointer           *old_items;
	gpointer           *new_items;
	gint                tail;
	gint                head;
	gint                count;

	g_return_if_fail (queue != NULL);

	priv = queue->priv;
	tail = priv->tail_idx;

	if (tail < (priv->head_idx + priv->mask)) {
		/* local push fast path */
		priv->items [tail & priv->mask] = data;
		g_atomic_int_set (&priv->tail_idx, tail + 1);
	}
	else {
		/* slow path, must resize the array */
		g_mutex_lock (priv->mutex);

		head = priv->head_idx;
		count = priv->tail_idx - priv->head_idx;
		old_items = priv->items;

		if (count >= priv->mask) {
			/* double the array size */
			new_items = g_malloc0 (sizeof (gpointer) * (priv->length << 1));

			/* copy the existing items over */
			memcpy (new_items,
			        old_items,
			        priv->length * sizeof (gpointer));

			/* assign the new array */
			priv->items = new_items;
			priv->head_idx = 0;
			priv->tail_idx = tail = count;
			priv->mask = (priv->mask << 1) | 1;
			priv->length = priv->length << 1;

			/* FIXME: Free old_items safely (can't really be done)
			 *   or save the buffer to a list for periodic GC.
			 *   However, as Shapor mentioned, leaking this will
			 *   only account for a total of 2x the largest growth
			 *   which may not be a problem anyway.
			 */
			/* g_free (old_items); */
		}

		priv->items [tail & priv->mask] = data;
		priv->tail_idx = tail + 1;

		g_mutex_unlock (priv->mutex);
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
	IrisWSQueuePrivate *priv;
	gpointer            result = NULL;
	gint                tail;

	g_return_val_if_fail (queue != NULL, NULL);

	priv = queue->priv;
	tail = priv->tail_idx;

	if (priv->head_idx >= tail)
		return NULL;

	tail -= 1;
	g_atomic_int_set (&priv->tail_idx, tail);

	if (priv->head_idx <= tail)
		result = priv->items [tail & priv->mask];
	else {
		g_mutex_lock (priv->mutex);

		if (priv->head_idx <= tail) {
			/* item is still available */
			result = priv->items [tail & priv->mask];
		}
		else {
			/* we lost the race, restore the tail */
			priv->tail_idx = tail + 1;
			result = NULL;
		}

		g_mutex_unlock (priv->mutex);
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
	IrisWSQueuePrivate *priv;
	gboolean            taken  = FALSE;
	gpointer            result = NULL;
	gint                head;

	#ifdef WIN32
	DWORD    wait_result;
	#else
	GTimeVal tv          = {0,0};
	#endif

	g_return_val_if_fail (queue != NULL, NULL);

	priv = queue->priv;

	/* GMutex does not export timedlock, so for now we will use
	 * the non-portable pthread call directly.
	 */
	#ifdef WIN32
	/* pthread_mutex_timedlock returns 0 on success */
	wait_result = WaitForSingleObject (
	              (CRITICAL_SECTION *)priv->mutex,
	              timeout);
	taken = (wait_result != WAIT_OBJECT_0);
	#else
	g_get_current_time (&tv);
	g_time_val_add (&tv, (G_USEC_PER_SEC / 1000) * timeout);

	taken = pthread_mutex_timedlock (
			(pthread_mutex_t*)priv->mutex,
			(struct timespec*)&tv);
	#endif

	head = priv->head_idx;
	g_atomic_int_set (&priv->head_idx, head + 1);

	if (head < priv->tail_idx)
		result = priv->items [head & priv->mask];
	else
		priv->head_idx = head;

	if (taken)
		g_mutex_unlock (priv->mutex);

	return result;
}
