/* iris-progress-monitor.c
 *
 * Copyright (C) 2009 Sam Thursfield <ssssam@gmail.com>
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

#include "iris-progress-monitor.h"
#include "iris-progress-monitor-private.h"

#include <stdio.h>

/**
 * SECTION:iris-progress-monitor
 * @title: IrisProgressMonitor
 * @short_description: Interface for progress monitor widgets
 * @see_also: #IrisProgressDialog, #IrisProgressInfoBar
 *
 * This interface is not of direct use unless you want to implement a new
 * monitoring widget, or monitor something other than an #IrisTask: you might
 * like #IrisProgressDialog or #IrisProgressInfoBar which are implementations.
 *
 * #IrisProgressMonitor is an interface for widgets which display the progress
 * of an action. It has support to watch any number of #IrisProcess objects
 * using the iris_progress_monitor_watch_process() and
 * iris_progress_monitor_watch_process_chain() functions.
 *
 * The interface has the flexibility to watch any #IrisTask objects if desired.
 * The task will need to call iris_progress_monitor_watch_callback()
 * periodically to update the progress monitor. This function must be called
 * from the GLib main loop; see the documentation for the function for more
 * information.
 *
 * This interface should only be called from the GLib main loop thread.
 *
 * FIXME: Write. Try actually using for an arbitrary task.
 */

enum {
	CANCEL,
	LAST_SIGNAL
};

static void iris_progress_monitor_base_init (gpointer g_class);

static guint signals[LAST_SIGNAL] = { 0 };

GType
iris_progress_monitor_get_type (void)
{
	static GType progress_monitor_type = 0;

	if (! progress_monitor_type) {
		const GTypeInfo progress_monitor_info = {
			sizeof (IrisProgressMonitorInterface), /* class_size */
			iris_progress_monitor_base_init,       /* base_init */
			NULL,                                  /* base_finalize */
			NULL,
			NULL,                                  /* class_finalize */
			NULL,                                  /* class_data */
			0,
			0,                                     /* n_preallocs */
			NULL
		};

		progress_monitor_type =
		  g_type_register_static (G_TYPE_INTERFACE, "IrisProgressMonitor",
		                          &progress_monitor_info, 0);

		g_type_interface_add_prerequisite (progress_monitor_type,
		                                   G_TYPE_OBJECT);
	}

	return progress_monitor_type;
}

static void
iris_progress_monitor_base_init (gpointer g_class)
{
	static gboolean initialized = FALSE;

	if (G_LIKELY(initialized))
		return;

	initialized = TRUE;

	/**
	* IrisProgressMonitor::cancel:
	*
	* Emitted when a 'cancel' button is pressed on a progress widget. Any
	* #IrisProcess objects being watched are cancelled automatically, but if
	* you are doing something more advanced you will need to handle this signal
	* yourself.
	*/
	signals[CANCEL] =
	  g_signal_new (("cancel"),
	                G_OBJECT_CLASS_TYPE (g_class),
	                G_SIGNAL_RUN_FIRST,
	                0,
	                NULL, NULL,
	                g_cclosure_marshal_VOID__VOID,
	                G_TYPE_NONE, 0);
}

IrisProgressWatch *
iris_progress_monitor_add_watch (IrisProgressMonitor *progress_monitor,
                                 const gchar         *title)
{
	IrisProgressMonitorInterface *iface;
	IrisProgressWatch *watch;

	watch = g_slice_new (IrisProgressWatch);
	watch->monitor = progress_monitor;

	watch->cancelled = FALSE;
	watch->complete  = FALSE;

	watch->processed_items = 0;
	watch->total_items     = 0;
	watch->fraction        = 0;

	watch->title = g_strdup (title);

	watch->process = NULL;

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE(progress_monitor);
	iface->add_watch (progress_monitor, watch);

	return watch;
}

static void
calculate_fraction (IrisProgressWatch *watch)
{
	if (watch->complete)
		watch->fraction = 1.0;
	else if (watch->processed_items == 0)
		watch->fraction = 0.0;
	else
		watch->fraction = (float)watch->processed_items / 
		                  (float)watch->total_items;
};

/**
 * iris_progress_monitor_update_watch:
 * @watch: an #IrisProgressWatch, as returned by
 *         iris_process_monitor_add_watch()
 * @processed_items: number of work items completed
 * @total_items: total number of work items
 *
 * This function allows you to use #IrisProgressMonitor widgets to watch any
 * kind of IrisTask. After adding a watch with
 * iris_progress_monitor_add_watch(), your task or task series should call this
 * function periodically to update the UI.
 **/
void
iris_progress_monitor_update_watch (IrisProgressWatch *watch,
                                    gint               processed_items,
                                    gint               total_items) 
{
	IrisProgressMonitor *progress_monitor = watch->monitor;
	IrisProgressMonitorInterface *iface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	watch->processed_items = processed_items;
	watch->total_items = total_items;

	calculate_fraction (watch);

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);
	iface->update_watch (progress_monitor, watch);
}

/**
 * iris_progress_monitor_watch_cancelled:
 * @task: Ignored. This parameter is present so the function can be added as a
 *        callback/errback to an #IrisTask.
 * @watch: An #IrisProgressWatch. 
 *
 * This function notifies the monitor when a watch is cancelled. One of
 * iris_progress_monitor_watch_cancelled() or
 * iris_progress_monitor_watch_complete() must be called for each watch before
 * the monitor widget will disappear.
 *
 * NOTE: #IrisTask and #IrisProcess objects do not call any callbacks or
 * errbacks when they are cancelled. If you are using #IrisProgressMonitor to
 * watch an #IrisTask, you need to call this function in the task function when
 * it responds to iris_task_is_cancelled() == %TRUE.
 **/
void
iris_progress_monitor_watch_cancelled (IrisTask *task,
                                       IrisProgressWatch *watch)
{
	IrisProgressMonitor *progress_monitor = watch->monitor;
	IrisProgressMonitorInterface *interface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));
	interface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	watch->cancelled = TRUE;

	interface->update_watch (progress_monitor, watch);
	interface->watch_stopped (progress_monitor);
}

/**
 * iris_progress_monitor_watch_complete:
 * @task: Ignored. This parameter is present so the function can be added as a
 *        callback/errback to an #IrisTask.
 * @watch: An #IrisProgressWatch. 
 *
 * This function notifies the monitor when a watch is complete.  One of
 * iris_progress_monitor_watch_cancelled() or
 * iris_progress_monitor_watch_complete() must be called for each watch before
 * the monitor widget will disappear.
 **/
void
iris_progress_monitor_watch_complete (IrisTask *task,
                                      IrisProgressWatch *watch)
{
	IrisProgressMonitor *progress_monitor = watch->monitor;
	IrisProgressMonitorInterface *interface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));
	interface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	watch->complete = TRUE;

	interface->update_watch (progress_monitor, watch);
	interface->watch_stopped (progress_monitor);
}

void
_iris_progress_watch_free (IrisProgressWatch *watch)
{
	g_free (watch->title);

	g_slice_free (IrisProgressWatch, watch);
}


/**
 * iris_progress_monitor_set_title:
 * @progress_monitor: An #IrisProgressMonitor
 * @title: String describing the overall activity the progress monitor watches.
 *
 * This function sets a global title for the @progress_monitor. For example, in
 * #IrisProgressDialog this function sets the title of the window.
 **/
void
iris_progress_monitor_set_title (IrisProgressMonitor *progress_monitor,
                                 const gchar *title)
{
	IrisProgressMonitorInterface *interface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	interface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	interface->set_title (progress_monitor, title);
};

/**
 * iris_progress_monitor_set_close_delay:
 * @progress_monitor: An #IrisProgressMonitor
 * @milliseconds: Time to wait before @progress_monitor destroys itself
 *
 * #IrisProgressDialog and #IrisProgressInfoBar will call gtk_widget_destroy()
 * on themselves when all of their watches complete. By default they will wait
 * for 0.5 seconds before doing so, mainly because the values they display are
 * often slightly behind the actual processes being watched and so they can
 * appear to have stopped without finishing. This function allows you to tweak
 * this behaviour.
 *
 * A timeout of 0 seconds will cause @progress_monitor to disappear as soon as
 * the process completes. A timeout < 0 will stop #IrisProgressDialog and
 * #IrisProgressInfoBar from destroying themselves at all.
 **/
void
iris_progress_monitor_set_close_delay (IrisProgressMonitor *progress_monitor,
                                       gint                 milliseconds)
{
	IrisProgressMonitorInterface *interface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	interface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	interface->set_close_delay (progress_monitor, milliseconds);
}


/**************************************************************************
 *                      IrisProcess watch functions                       *
 *************************************************************************/

/* IrisProgressMonitor can watch any kind of IrisTask, but we have special
 * support for IrisProcess. */

static void
process_source_connected (IrisProcess *process,
                          IrisProcess *source,
                          gpointer user_data)
{
	IrisProgressMonitor *progress_monitor = IRIS_PROGRESS_MONITOR (user_data);

	/* FIXME: have a way of ensuring it appears before 'process' in the dialog */
	iris_progress_monitor_watch_process_chain (progress_monitor, source);
}

static void
process_sink_connected (IrisProcess *process,
                        IrisProcess *sink,
                        gpointer user_data)
{
	IrisProgressMonitor *progress_monitor = IRIS_PROGRESS_MONITOR (user_data);

	/* FIXME: have a way of ensuring it appears after 'process' in the dialog */
	iris_progress_monitor_watch_process_chain (progress_monitor, sink);
}

static void
process_watch_callback (IrisProcess *process,
                        gint         processed_items,
                        gint         total_items,
                        gboolean     cancelled,
                        gpointer     user_data)
{
	IrisProgressMonitor          *progress_monitor;
	IrisProgressMonitorInterface *interface;
	IrisProgressWatch            *watch = user_data;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (watch->monitor));

	progress_monitor = watch->monitor;
	interface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	if (cancelled)
		iris_progress_monitor_watch_cancelled (IRIS_TASK (process),
		                                       watch);

	watch->processed_items = processed_items;
	watch->total_items = total_items;

	calculate_fraction (watch);

	interface->update_watch (progress_monitor, watch);
}

void
iris_progress_monitor_watch_process (IrisProgressMonitor *progress_monitor,
                                     IrisProcess         *process)
{
	IrisProgressMonitorInterface *iface;
	IrisProgressWatch *watch;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));
	g_return_if_fail (IRIS_IS_PROCESS (process));

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE(progress_monitor);

	if (iface->is_watching_process (progress_monitor, process))
		return;

	g_signal_connect_object (process, "source-connected",
	                         G_CALLBACK (process_source_connected),
	                         G_OBJECT (progress_monitor), 0);
	g_signal_connect_object (process, "sink-connected",
	                         G_CALLBACK (process_sink_connected),
	                         G_OBJECT (progress_monitor), 0);

	/* Add watch to implementation */
	watch = iris_progress_monitor_add_watch (progress_monitor,
	                                         iris_process_get_title (process));
	watch->process = process;

	iris_task_add_both (IRIS_TASK (process),
	                    (IrisTaskFunc)iris_progress_monitor_watch_complete,
	                    (IrisTaskFunc)iris_progress_monitor_watch_cancelled,
	                    watch, NULL);

	/* Start process updating the implementation using callback */
	iris_process_add_watch_callback (process, process_watch_callback,
	                                 watch, NULL);
}

void
iris_progress_monitor_watch_process_chain (IrisProgressMonitor *progress_monitor,
                                           IrisProcess         *process)
{
	IrisProcess *head;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));
	g_return_if_fail (IRIS_IS_PROCESS (process));

	head = process;
	while (iris_process_has_predecessor (head))
		head = iris_process_get_predecessor (head);

	process = head;
	do {
		iris_progress_monitor_watch_process (progress_monitor, process);
	} while ((process = iris_process_get_successor (process)) != NULL);
}


/**************************************************************************
 *                    Widget implementation helpers                       *
 *************************************************************************/

/* Return TRUE if a progress monitor implementation can close, based on the
 * list of activities it is watching.
 */
gboolean
_iris_progress_monitor_watch_list_finished (GList *watch_list)
{
	GList *node;

	for (node=watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		if ((!watch->complete) && (!watch->cancelled))
			return FALSE;

		/* It is possible that we are watching one process, which has now been
		 * connected to some others but we are still waiting for our signal to
		 * come through because it's being sent via a g_idle. So we check that
		 * the source doesn't have any successors.
		 */
		if (watch->process != NULL) {
			IrisProcess *sink = iris_process_get_successor (watch->process);
			if (sink != NULL && !iris_process_is_finished (sink))
				return FALSE;
		}
	}

	return TRUE;
};

void
_iris_progress_monitor_cancel (IrisProgressMonitor *progress_monitor,
                               GList               *process_watch_list)
{
	GList *node;

	/* Cancel every process being watched */
	for (node=process_watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		if (watch->process != NULL)
			iris_process_cancel (watch->process);
	}

	g_signal_emit (progress_monitor, signals[CANCEL], 0);
}