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
 * receives #IRIS_PROGRESS_MESSAGE_CANCELLED or #IRIS_PROGRESS_MESSAGE_COMPLETE.
 *
 * <refsect2 id="lifecycle">
 * <title>Lifecycle</title>
 * <para>
 * The typical use-case for #IrisProgressMonitor is that you want for example
 * an #IrisProgressInfoBar in your application's main window that displays any
 * slow processes that are currently in progress. If no processes are running
 * the #IrisProgressInfoBar should be hidden. The recommended way to achieve
 * this is by creating one #IrisProgressInfoBar object during the initialization
 * of your program and then showing and hiding this one widget appropriately.
 * You can get the widget to handle this automatically, by calling
 * iris_progress_monitor_set_permanent_mode(). The widget will then make sure it
 * is visible (using gtk_widget_show() in the case of the Gtk+ progress
 * monitors) whenever it has any watches active, and will hide itself again
 * when the last process it is watching finishes.
 *
 * A more powerful mechanism exists if you want to go beyond this behaviour:
 * when all of the watches have completed, the #IrisProgressMonitor::finished
 * signal is emitted. For example, you could connect this signal to
 * gtk_widget_destroy() to make the widget finalize itself when it has no more
 * active processes to watch.
 * </para>
 * </refsect2>
 *
 */

/* Signal id's */
enum {
	CANCEL,
	FINISHED,
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
	* @progress_monitor: the #IrisProgressMonitor that received the signal
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

	/**
	* IrisProgressMonitor::finished:
	* @progress_monitor: the #IrisProgressMonitor that received the signal
	*
	* Emitted when all watches being monitored have completed. It is not
	* to add new watches during emission of this signal.
	*/
	signals[FINISHED] =
	  g_signal_new (("finished"),
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

	watch = g_slice_new0 (IrisProgressWatch);
	watch->monitor = progress_monitor;
	watch->port = iris_port_new ();

	watch->display_style = display_style;

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
 * #IRIS_PROGRESS_MESSAGE_CANCELLED or #IRIS_PROGRESS_MESSAGE_COMPLETE.
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
 * iris_progress_monitor_set_permanent_mode:
 * @progress_monitor: an #IrisProgressMonitor
 * @enable: whether to enable permanent mode
 *
 * Permanent mode causes the progress monitor to automatically show itself when
 * any watches are added, and hide itself again when all of them have finished.
 * Using this feature it is simple to create one #IrisProgressMonitor instance
 * at the start of your application and use it to display progress of every
 * #IrisTask and #IrisProcess you create.
 *
 * #IrisProgressMonitor::finished is still emitted when permanent mode is
 * enabled, and the callbacks run <emphasis>before</emphasis> the progress
 * monitor is hidden.
 */
void
iris_progress_monitor_set_permanent_mode (IrisProgressMonitor *progress_monitor,
                                          gboolean             enable)
{
	IrisProgressMonitorInterface *interface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	interface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	interface->set_permanent_mode (progress_monitor, enable);
}

/**
 * iris_progress_monitor_set_watch_hide_delay:
 * @progress_monitor: an #IrisProgressMonitor
 * @milliseconds: time in milliseconds to display progress information for a task
 *             or process after it has completed or been cancelled. Default
 *             value: 500 ms.
 *
 * It is slightly visually jarring for a process or group of processes that are
 * being watched to disappear instantly when they complete or the cancel button
 * is clicked. You should not normally need to change the default value of
 * 500ms, which allows time for the UI to catch up with the process and the
 * user to catch up with the UI.
 **/
/* If milliseconds == -1 the watches will never be cleared. This is not
 * documented because for it to be useful there would probably need to be a way
 * to manually clear the watches. It is used for unit tests to check what is
 * being displayed in the UI.
 */
void
iris_progress_monitor_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                            int                  milliseconds)
{
	IrisProgressMonitorInterface *interface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	interface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	interface->set_watch_hide_delay (progress_monitor, milliseconds);
}


/**************************************************************************
 *                      IrisProcess watch functions                       *
 *************************************************************************/

/* IrisProgressMonitor can watch any kind of IrisTask, but we have special
 * support for IrisProcess. */

static IrisProgressWatch *
watch_process_internal (IrisProgressMonitor             *progress_monitor,
                        IrisProcess                     *process,
                        IrisProgressMonitorDisplayStyle  display_style,
                        gboolean                         chain)
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
	watch->chain_flag = chain;

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
	watch_process_internal (progress_monitor, process, display_style, FALSE);
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
	watch_process_internal (progress_monitor, process, display_style, TRUE);
}


/**************************************************************************
 *                    Widget implementation helpers                       *
 *************************************************************************/

/*void
_iris_progress_monitor_cancel (IrisProgressMonitor *progress_monitor,
                               GList               *watch_list)
{
	GList *node;

	* Cancel every process being watched *
	for (node=watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;
		iris_task_cancel (IRIS_TASK (watch->task));
	}

	g_signal_emit (progress_monitor, signals[CANCEL], 0);
}*/

void
_iris_progress_monitor_finished (IrisProgressMonitor *progress_monitor)
{
	g_return_if_fail (IRIS_PROGRESS_MONITOR (progress_monitor));

	g_signal_emit (progress_monitor, signals[FINISHED], 0);
}

/* Add watches for connected processes, called once we know the connections
 * cannot change */
static void
watch_chain (IrisProgressMonitor             *progress_monitor,
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
		                        display_style,
		                        FALSE);
	} while ((process = iris_process_get_successor (process)) != NULL);

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

static void
handle_title (IrisProgressWatch *watch,
              IrisMessage       *message)
{
	const char *title = g_value_get_string (iris_message_get_data (message));

	g_free (watch->title);
	watch->title = g_strdup (title);
}

void
_iris_progress_monitor_handle_message (IrisMessage  *message,
                                       gpointer      user_data)
{
	IrisProgressWatch            *watch = user_data;
	IrisProgressMonitor          *progress_monitor = watch->monitor;
	IrisProgressMonitorInterface *iface;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	/* Any of these messages indicate that the connections cannot now change. Note that for
	 * watch->chain_flag to be TRUE if watch->task is not of type IrisProcess is an error. 
	 * Normally you must be aware that watch->task could be an IrisTask.
	 */
	if (watch->chain_flag) {
		watch->chain_flag = FALSE;
		watch_chain (progress_monitor, IRIS_PROCESS (watch->task),
		             watch->display_style);
	}

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
		case IRIS_PROGRESS_MESSAGE_TITLE:
			handle_title (watch, message);
			break;
		default:
			g_warn_if_reached ();
	}

	/* Chain the message to implementation, after we have processed what we need */
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
