/* iris-gmainscheduler.c
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
#include "iris-gmainscheduler.h"
#include "iris-scheduler-private.h"
#include "iris-scheduler-manager.h"
#include "iris-gmainscheduler.h"
#include "iris-gsource.h"

/**
 * SECTION:iris-gmainscheduler
 * @short_description: A thread-less scheduler that executes in the main thread
 *
 * When writing fully asynchronous applications, it can make sense to have all
 * of your actual work items run from with in the main thread if they are not
 * thread-safe.  This scheduler will execute your work items in the main
 * thread.
 */

struct _IrisGMainSchedulerPrivate
{
	GMainContext *context;
	IrisQueue    *queue;
	guint         source;
};

G_DEFINE_TYPE (IrisGMainScheduler, iris_gmainscheduler, IRIS_TYPE_SCHEDULER);

static void
iris_gmainscheduler_queue_real (IrisScheduler  *scheduler,
                                IrisCallback    func,
                                gpointer        data,
                                GDestroyNotify  notify)
{
	IrisGMainSchedulerPrivate *priv;
	IrisThreadWork            *thread_work;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (func != NULL);

	priv = IRIS_GMAINSCHEDULER (scheduler)->priv;

	g_return_if_fail (priv->source != 0);

	thread_work = iris_thread_work_new (func, data);
	iris_queue_push (priv->queue, thread_work);
	g_main_context_wakeup (priv->context);
}

static void
iris_gmainscheduler_remove_thread_real (IrisScheduler *scheduler,
                                        IrisThread    *thread)
{
}

static void
iris_gmainscheduler_add_thread_real (IrisScheduler  *scheduler,
                                     IrisThread     *thread)
{
}

static void
iris_gmainscheduler_finalize (GObject *object)
{
	IrisGMainSchedulerPrivate *priv;

	priv = IRIS_GMAINSCHEDULER (object)->priv;

	priv->source = 0;
	priv->context = NULL;
	iris_queue_unref (priv->queue);
	priv->queue = NULL;

	G_OBJECT_CLASS (iris_gmainscheduler_parent_class)->finalize (object);
}

static void
iris_gmainscheduler_class_init (IrisGMainSchedulerClass *klass)
{
	GObjectClass       *object_class;
	IrisSchedulerClass *sched_class;

	sched_class = IRIS_SCHEDULER_CLASS (klass);
	sched_class->queue = iris_gmainscheduler_queue_real;
	sched_class->add_thread = iris_gmainscheduler_add_thread_real;
	sched_class->remove_thread = iris_gmainscheduler_remove_thread_real;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = iris_gmainscheduler_finalize;
	g_type_class_add_private (object_class, sizeof (IrisGMainSchedulerPrivate));
}

static void
iris_gmainscheduler_init (IrisGMainScheduler *scheduler)
{
	scheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheduler,
	                                               IRIS_TYPE_GMAINSCHEDULER,
	                                               IrisGMainSchedulerPrivate);
	scheduler->priv->queue = iris_queue_new ();
}

static gboolean
iris_gmainscheduler_source_cb (gpointer data)
{
	IrisGMainSchedulerPrivate *priv;
	IrisThreadWork            *thread_work;

	g_return_val_if_fail (data != NULL, FALSE);

	priv = IRIS_GMAINSCHEDULER (data)->priv;

	while ((thread_work = iris_queue_try_pop (priv->queue)) != NULL) {
		iris_thread_work_run (thread_work);
		iris_thread_work_free (thread_work);
	}

	return TRUE;
}

static void
iris_gmainscheduler_set_context (IrisGMainScheduler *scheduler,
                                 GMainContext       *context)
{
	IrisGMainSchedulerPrivate *priv;

	priv = scheduler->priv;

	if (!context)
		context = g_main_context_default ();

	if ((priv->context = context) != NULL) {
		priv->source = iris_gsource_new (priv->queue,
						 context,
						 iris_gmainscheduler_source_cb,
						 scheduler);
	}

	if (!priv->source)
		g_warning ("Error creating IrisGSource");
}

/**
 * iris_gmainscheduler_new:
 * @context: An optional #GMainContext or %NULL
 *
 * Creates a new instance of #IrisGMainScheduler.  If @context is %NULL, then
 * the default #GMainContext will be used.
 *
 * See g_main_context_default().
 *
 * Return value: the newly created instance of #IrisGMainScheduler
 */
IrisScheduler*
iris_gmainscheduler_new (GMainContext *context)
{
	IrisGMainScheduler *scheduler;

	scheduler = g_object_new (IRIS_TYPE_GMAINSCHEDULER, NULL);
	iris_gmainscheduler_set_context (scheduler, context);

	return IRIS_SCHEDULER (scheduler);
}

/**
 * iris_gmainscheduler_get_context:
 * @gmain_scheduler: An #IrisGMainScheduler
 *
 * Return value: The #GMainContext instance for this scheduler
 */
GMainContext*
iris_gmainscheduler_get_context (IrisGMainScheduler *gmain_scheduler)
{
	g_return_val_if_fail (IRIS_IS_GMAINSCHEDULER (gmain_scheduler), NULL);
	return gmain_scheduler->priv->context;
}
