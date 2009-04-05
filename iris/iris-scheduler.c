/* iris-scheduler.c
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

#ifdef LINUX
#include <sys/sysinfo.h>
#endif

#include "iris-scheduler.h"
#include "iris-scheduler-private.h"
#include "iris-scheduler-manager.h"

struct _IrisSchedulerPrivate
{
	GMutex      *mutex;

	GAsyncQueue *queue;

	gboolean     initialized;

	guint        min_threads;
	guint        max_threads;
};

G_DEFINE_TYPE (IrisScheduler, iris_scheduler, G_TYPE_OBJECT);

static void
iris_scheduler_queue_real (IrisScheduler  *scheduler,
                           IrisCallback    func,
                           gpointer        data,
                           GDestroyNotify  notify)
{
	IrisSchedulerPrivate *priv;
	IrisThreadWork       *thread_work;

	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));
	g_return_if_fail (func != NULL);

	priv = scheduler->priv;

	g_return_if_fail (priv->queue != NULL);

	/* All of our threads are reading from our one queue.  Probably
	 * not a good idea for long term.  We should implement the work
	 * stealing queue for this.
	 */
	thread_work = iris_thread_work_new (func, data);
	g_async_queue_push (priv->queue, thread_work);
}

static guint
iris_scheduler_get_n_cpu (void)
{
#ifdef LINUX
	static gint n_cpu = 0;
	if (G_UNLIKELY (n_cpu == 0))
		n_cpu = get_nprocs ();
	return n_cpu;
#else
	return 1;
#endif
}

static gint
iris_scheduler_get_min_threads_real (IrisScheduler *scheduler)
{
	gint min_threads;
	min_threads = scheduler->priv->min_threads;
	return (min_threads > 0) ? min_threads : 1;
}

static gint
iris_scheduler_get_max_threads_real (IrisScheduler *scheduler)
{
	gint max_threads;
	if ((max_threads = scheduler->priv->max_threads))
		return max_threads;
	return MAX (2, iris_scheduler_get_n_cpu ());
}

static void
iris_scheduler_add_thread_real (IrisScheduler  *scheduler,
                                IrisThread     *thread)
{
	IrisSchedulerPrivate *priv;

	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	priv = scheduler->priv;

	iris_thread_manage (thread, priv->queue);
}

static void
iris_scheduler_finalize (GObject *object)
{
	IrisSchedulerPrivate *priv;

	priv = IRIS_SCHEDULER (object)->priv;

	g_mutex_free (priv->mutex);

	G_OBJECT_CLASS (iris_scheduler_parent_class)->finalize (object);
}

static void
iris_scheduler_class_init (IrisSchedulerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->queue = iris_scheduler_queue_real;
	klass->get_min_threads = iris_scheduler_get_min_threads_real;
	klass->get_max_threads = iris_scheduler_get_max_threads_real;
	klass->add_thread = iris_scheduler_add_thread_real;

	object_class->finalize = iris_scheduler_finalize;

	g_type_class_add_private (object_class, sizeof (IrisSchedulerPrivate));
}

static void
iris_scheduler_init (IrisScheduler *scheduler)
{
	scheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheduler,
	                                          IRIS_TYPE_SCHEDULER,
	                                          IrisSchedulerPrivate);
	scheduler->priv->mutex = g_mutex_new ();
	scheduler->priv->queue = g_async_queue_new ();
	scheduler->priv->min_threads = 0;
	scheduler->priv->max_threads = 0;
}

IrisScheduler*
iris_scheduler_new (void)
{
	return g_object_new (IRIS_TYPE_SCHEDULER, NULL);
}

IrisScheduler*
iris_scheduler_new_full (guint min_threads,
                         guint max_threads)
{
	IrisScheduler *scheduler;

	scheduler = iris_scheduler_new ();
	scheduler->priv->min_threads = min_threads;
	scheduler->priv->max_threads = max_threads;

	return scheduler;
}

void
iris_scheduler_queue (IrisScheduler  *scheduler,
                      IrisCallback    func,
                      gpointer        data,
                      GDestroyNotify  notify)
{
	IrisSchedulerPrivate *priv;

	priv = scheduler->priv;

	/* Lazy initialization of the scheduler. By holding off until we
	 * need this, we attempt to reduce our total thread usage.
	 */

	if (G_UNLIKELY (!priv->initialized)) {
		g_mutex_lock (priv->mutex);
		if (G_LIKELY (!priv->initialized)) {
			iris_scheduler_manager_prepare (scheduler);
			g_atomic_int_set (&priv->initialized, TRUE);
		}
		g_mutex_unlock (priv->mutex);
	}

	IRIS_SCHEDULER_GET_CLASS (scheduler)->queue (scheduler, func, data, notify);
}

gint
iris_scheduler_get_max_threads (IrisScheduler *scheduler)
{
	return IRIS_SCHEDULER_GET_CLASS (scheduler)->get_max_threads (scheduler);
}

gint
iris_scheduler_get_min_threads (IrisScheduler *scheduler)
{
	return IRIS_SCHEDULER_GET_CLASS (scheduler)->get_min_threads (scheduler);
}

void
iris_scheduler_add_thread (IrisScheduler *scheduler,
                           IrisThread    *thread)
{
	IRIS_SCHEDULER_GET_CLASS (scheduler)->add_thread (scheduler, thread);
}

void
iris_scheduler_remove_thread (IrisScheduler *scheduler,
                              IrisThread    *thread)
{
	IRIS_SCHEDULER_GET_CLASS (scheduler)->remove_thread (scheduler, thread);
}
