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

/* GMainScheduler is a little unhappy as a subclass of IrisScheduler, it might
 * better to make IrisScheduler an interface and add some IrisSimpleScheduler
 * as the default implementation ... but no huge need
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
                                GDestroyNotify  destroy_notify)
{
	IrisGMainSchedulerPrivate *priv;
	IrisThreadWork            *thread_work;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (func != NULL);

	priv = IRIS_GMAINSCHEDULER (scheduler)->priv;

	g_return_if_fail (priv->source != 0);

	thread_work = iris_thread_work_new (func, data, destroy_notify);
	iris_queue_push (priv->queue, thread_work);
	g_main_context_wakeup (priv->context);
}

static gboolean
iris_gmainscheduler_unqueue_real (IrisScheduler *scheduler,
                                  gpointer       work_item)
{
	IrisThreadWork            *thread_work = (IrisThreadWork *)work_item;

	g_return_val_if_fail (scheduler != NULL, FALSE);
	g_return_val_if_fail (thread_work != NULL, FALSE);

	thread_work->remove = TRUE;

	return TRUE;
}

static void
iris_gmainscheduler_foreach_real (IrisScheduler            *scheduler,
                                  IrisSchedulerForeachFunc  callback,
                                  gpointer                  user_data)
{
	IrisGMainSchedulerPrivate *priv;
	gboolean continue_flag;
	gint     i;

	g_return_if_fail (scheduler != NULL);
	g_return_if_fail (callback != NULL);

	priv = IRIS_GMAINSCHEDULER (scheduler)->priv;

	/* Foreach the queue in a really hacky way. FIXME: don't do it like this! */
	for (i=0; i<iris_queue_get_length (priv->queue); i++) {
		IrisThreadWork *thread_work;

		thread_work = iris_queue_try_pop (priv->queue);

		if (!thread_work) break;

		continue_flag = callback (scheduler,
		                          thread_work,
		                          thread_work->callback,
		                          thread_work->data,
		                          user_data);

		if (thread_work->remove == FALSE)
			iris_queue_push (priv->queue, thread_work);
		else {
			iris_thread_work_free (thread_work);
		}

		if (!continue_flag)
			return;
	}
};


static void
iris_gmainscheduler_remove_thread_real (IrisScheduler *scheduler,
                                        IrisThread    *thread)
{
}

static void
iris_gmainscheduler_add_thread_real (IrisScheduler  *scheduler,
                                     IrisThread     *thread,
                                     gboolean        exclusive)
{
}

static void
iris_gmainscheduler_finalize (GObject *object)
{
	IrisGMainSchedulerPrivate *priv;

	priv = IRIS_GMAINSCHEDULER (object)->priv;

	priv->source = 0;
	priv->context = NULL;
	g_object_unref (priv->queue);
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
	sched_class->unqueue = iris_gmainscheduler_unqueue_real;
	sched_class->foreach = iris_gmainscheduler_foreach_real;
	sched_class->add_thread = iris_gmainscheduler_add_thread_real;
	sched_class->remove_thread = iris_gmainscheduler_remove_thread_real;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = iris_gmainscheduler_finalize;
	g_type_class_add_private (object_class, sizeof (IrisGMainSchedulerPrivate));
}

static void
iris_gmainscheduler_init (IrisGMainScheduler *gmainscheduler)
{
	gmainscheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (gmainscheduler,
	                                                     IRIS_TYPE_GMAINSCHEDULER,
	                                                     IrisGMainSchedulerPrivate);
	gmainscheduler->priv->queue = iris_queue_new ();

	/* Don't add us to the scheduler manager, since we don't need any threads managing */
	IRIS_SCHEDULER(gmainscheduler)->priv->initialized = TRUE;
}

static gboolean
iris_gmainscheduler_source_cb (gpointer data)
{
	IrisGMainSchedulerPrivate *priv;
	IrisThreadWork            *thread_work;

	g_return_val_if_fail (data != NULL, FALSE);

	priv = IRIS_GMAINSCHEDULER (data)->priv;

	while ((thread_work = iris_queue_try_pop (priv->queue)) != NULL) {
		if (!thread_work->remove)
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
