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

#include <glib/gi18n.h>

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
 * monitoring widget, or monitor something other than an #IrisTask - you might
 * like #IrisProgressDialog or #IrisProgressInfoBar which are implementations.
 *
 * #IrisProgressMonitor is an interface for widgets which display the progress
 * of an action. It has support to watch any number of #IrisProcess objects
 * using the iris_progress_monitor_watch_process() and
 * iris_progress_monitor_watch_process_chain() functions.
 *
 * The interface has the flexibility to watch any #IrisTask objects if desired.
 * To do this you must first call iris_progress_monitor_add_watch(). This
 * returns an #IrisPort object where you should then send messages of
 * #IrisProgressMessageType. The progress monitor will watch the task until it
 * receives @IRIS_PROGRESS_MESSAGE_CANCELLED or @IRIS_PROGRESS_MESSAGE_COMPLETE.
 *
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


static IrisProgressWatch *
iris_progress_monitor_add_watch_internal (IrisProgressMonitor             *progress_monitor,
                                          IrisTask                        *task,
                                          IrisProgressMonitorDisplayStyle  display_style,
                                          const gchar                     *title)
{
	IrisProgressMonitorInterface *iface;
	IrisProgressWatch *watch;

	watch = g_slice_new (IrisProgressWatch);
	watch->monitor = progress_monitor;
	watch->port = iris_port_new ();

	watch->cancelled = FALSE;
	watch->complete  = FALSE;

	watch->display_style = display_style;

	watch->processed_items = 0;
	watch->total_items     = 0;
	watch->fraction        = 0;

	watch->title = g_strdup (title);

	watch->task = task;

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE(progress_monitor);
	iface->add_watch (progress_monitor, watch);

	return watch;
}

/**
 * iris_progress_monitor_add_watch:
 * @progress_monitor: an #IrisProgressMonitor
 * @task: the #IrisTask that is being watched. This gets used if the cancel
 *        button is pressed, and to make sure the same task is not watched
 *        multiple times.
 * @display_style: format for the textual description, see
 *                 #IrisProgressMonitorDisplayStyle.
 * @title: name of the task being watched, used to label its progress bar, or
 *         %NULL.
 *
 * Causes @progress_monitor to add a new watch (generally represented by
 * a progress bar) for a task, which needs to update it by posting messages on
 * the #IrisPort that is returned. These messages should be from
 * #IrisProgressMessageType. The watch will be removed when the port receives
 * @IRIS_PROGRESS_MESSAGE_CANCELLED or @IRIS_PROGRESS_MESSAGE_COMPLETE.
 *
 * Returns: an #IrisPort, listening for progress messages.
 *          
 **/
IrisPort *
iris_progress_monitor_add_watch (IrisProgressMonitor             *progress_monitor,
                                 IrisTask                        *task,
                                 IrisProgressMonitorDisplayStyle  display_style,
                                 const gchar                     *title)
{
	IrisProgressWatch            *watch;
	IrisProgressMonitorInterface *iface;

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	if (iface->is_watching_task (progress_monitor, task))
		return NULL;

	watch = iris_progress_monitor_add_watch_internal (progress_monitor,
	                                                  task,
	                                                  display_style, 
	                                                  title);

	return watch->port;
}

void
_iris_progress_watch_free (IrisProgressWatch *watch)
{
	g_free (watch->title);

	g_object_unref (watch->port);

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
 * the behaviour.
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

static IrisProgressWatch *
watch_process_internal (IrisProgressMonitor             *progress_monitor,
                        IrisProcess                     *process,
                        IrisProgressMonitorDisplayStyle  display_style)
{
	IrisProgressMonitorInterface *iface;
	IrisProgressWatch *watch;

	g_return_val_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor), NULL);
	g_return_val_if_fail (IRIS_IS_PROCESS (process), NULL);

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE(progress_monitor);

	if (iface->is_watching_task (progress_monitor, IRIS_TASK (process)))
		return NULL;

	/* Add watch to implementation */
	watch = iris_progress_monitor_add_watch_internal
	          (progress_monitor,
	           IRIS_TASK (process),
	           display_style,
	           iris_process_get_title (process));

	watch->display_style = display_style;

	iris_process_add_watch (process, watch->port);

	return watch;
}


/**
 * iris_progress_monitor_watch_process:
 * @progress_monitor: An #IrisProgressMonitor
 * @process: an #IrisProcess
 * @display_style: format to display progress, such as a percentage or
 *                 as items/total. See #IrisProgressMonitorDisplayStyle.
 *
 * Visually display the progress of @process. If @process has source or sink
 * processes connected, they are ignored.
 **/
void
iris_progress_monitor_watch_process (IrisProgressMonitor             *progress_monitor,
                                     IrisProcess                     *process,
                                     IrisProgressMonitorDisplayStyle  display_style)
{
	watch_process_internal (progress_monitor, process, display_style);
}

/**
 * iris_progress_monitor_watch_process_chain:
 * @progress_monitor: An #IrisProgressMonitor
 * @process: an #IrisProcess
 * @display_style: format to display progress, such as a percentage or
 *                 as items/total. See #IrisProgressMonitorDisplayStyle.
 *
 * Visually display the progress of @process, and any processes which are
 * connected. Each process in the chain is displayed separately in the order of
 * the data flow.
 **/
void
iris_progress_monitor_watch_process_chain (IrisProgressMonitor             *progress_monitor,
                                           IrisProcess                     *process,
                                           IrisProgressMonitorDisplayStyle  display_style)
{
	IrisProcess *head;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));
	g_return_if_fail (IRIS_IS_PROCESS (process));

	head = process;
	while (iris_process_has_predecessor (head))
		head = iris_process_get_predecessor (head);

	process = head;
	do {
		watch_process_internal (progress_monitor,
		                        process, 
		                        display_style);
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
	}

	return TRUE;
};

void
_iris_progress_monitor_cancel (IrisProgressMonitor *progress_monitor,
                               GList               *watch_list)
{
	GList *node;

	/* Cancel every process being watched */
	for (node=watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;
		iris_task_cancel (IRIS_TASK (watch->task));
	}

	g_signal_emit (progress_monitor, signals[CANCEL], 0);
}

static void
handle_cancelled (IrisProgressWatch *watch,
                  IrisMessage *message)
{
	watch->cancelled = TRUE;
}

static void
handle_complete (IrisProgressWatch *watch,
                 IrisMessage *message)
{
	watch->complete = TRUE;
}

static void
handle_fraction (IrisProgressWatch *watch,
                 IrisMessage *message)
{
	const GValue *value = iris_message_get_data (message);
	watch->fraction = g_value_get_float (value);
}

static float
calculate_fraction (IrisProgressWatch *watch)
{
	if (watch->complete)
		return 1.0;
	else if (watch->processed_items == 0 || watch->total_items == 0)
		return 0.0;
	else {
		g_return_val_if_fail (watch->processed_items <= watch->total_items, 1.0);

		return (float)watch->processed_items / 
		       (float)watch->total_items;
	}
};

static void
handle_processed_items (IrisProgressWatch *watch,
                        IrisMessage *message)
{
	const GValue *value = iris_message_get_data (message);
	watch->processed_items = g_value_get_int (value);
	watch->fraction = calculate_fraction (watch);
}

static void
handle_total_items (IrisProgressWatch *watch,
                    IrisMessage *message)
{
	const GValue *value = iris_message_get_data (message);
	watch->total_items = g_value_get_int (value);
	watch->fraction = calculate_fraction (watch);
}

void
_iris_progress_monitor_handle_message (IrisMessage  *message,
                                       gpointer      user_data)
{
	IrisProgressWatch            *watch = user_data;
	IrisProgressMonitor          *progress_monitor = watch->monitor;
	IrisProgressMonitorInterface *iface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	switch (message->what) {
		case IRIS_PROGRESS_MESSAGE_CANCELLED:
			handle_cancelled (watch, message);
			break;
		case IRIS_PROGRESS_MESSAGE_COMPLETE:
			handle_complete (watch, message);
			break;
		case IRIS_PROGRESS_MESSAGE_FRACTION:
			handle_fraction (watch, message);
			break;
		case IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS:
			handle_processed_items (watch, message);
			break;
		case IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS:
			handle_total_items (watch, message);
			break;
		default:
			g_warn_if_reached ();
	}

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);
	iface->handle_message (progress_monitor, watch, message);
}

void
_iris_progress_monitor_format_watch (IrisProgressMonitor *progress_monitor,
                                     IrisProgressWatch   *watch,
                                     gchar               *progress_text)
{
	if (watch->complete) {
		g_snprintf (progress_text, 255, _("Complete"));
		return;
	} else if (watch->cancelled) {
		g_snprintf (progress_text, 255, _("Cancelled"));
		return;
	}

	switch (watch->display_style) {
		case IRIS_PROGRESS_MONITOR_ITEMS:
			g_snprintf (progress_text, 255, _("%i items of %i"), 
			            watch->processed_items,
			            watch->total_items);
			break;
		case IRIS_PROGRESS_MONITOR_PERCENTAGE:
			g_snprintf (progress_text, 255, _("%.0f%% complete"), 
			            (watch->fraction * 100.0));
			break;
		default:
			g_warning ("Unrecognised value for watch->display_style.\n");
			progress_text[0] = 0;
	}
}
