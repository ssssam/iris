/* iris-progress-monitor.c
 *
 * Copyright (C) 2009-11 Sam Thursfield <ssssam@gmail.com>
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
 * @see_also: #GtkIrisProgressDialog, #GtkIrisProgressInfoBar
 *
 * #IrisProgressMonitor is a generic interface for widgets that display the
 * progress of #IrisProcess<!-- -->es and #IrisTask<!-- -->s. For simple use,
 * just create an instance of a progress manager widget (#GtkIrisProgressDialog
 * or #GtkIrisProgressInfoBar) and call iris_progress_monitor_watch_process()
 * or iris_progress_monitor_watch_process_chain().
 *
 * <refsect2 id="grouping">
 * <title>Grouping</title>
 * <para>
 * If you have more than one or two watches active, it is a good idea to group
 * them together: this improves visual presentation and avoids the danger of
 * your app displaying an enourmous #GtkIrisProgressInfoBar that shows six
 * processes. The exact way grouping is handled depends on the
 * #IrisProgressMonitor widget, but a good example is #GtkIrisProgressInfoBar,
 * where grouping is very important as the the ideal info bar takes up very
 * little space. By default an #GtkIrisProgressInfoBar will show a group as
 * just a title and an progress bar showing for overall completion; an
 * expander arrow allows the user to see details if they wish.
 *
 * If you have a series of connected process, or <firstterm>chain</firstterm>,
 * they will often fit nicely into their own group. If you use
 * iris_progress_monitor_watch_process_chain() you don't have to worry about
 * the #IrisProgressGroup object at all, or for more complicated setups you
 * must first call iris_progress_monitor_add_group() and then pass the result
 * to the the appropriate method.
 *
 * A watch group has both a <firstterm>title</firstterm> and a
 * <firstterm>plural</firstterm>; the plural is currently not
 * implemented but will be used to group together similar watch groups (to
 * further prevent giant #GtkIrisProgressInfoBar<!-- -->s).
 * For example, if you have several groups of processes each reading a
 * location on disk, you could give them the plural "Reading directories", and
 * #GtkIrisProgressInfoBar would be able to display them as just one progress
 * group unless the user clicked on the expander to view the details.
 *
 * The #IrisProgressWatch object will not be freed automatically. If you have
 * added all of the watches to a group, you can drop your reference using
 * iris_progress_group_unref() and then the group object will be freed
 * automatically when the work completes. If you keep this reference, the object
 * will not be freed and you can carry on using it.
 * </para>
 * </refsect2>
 *
 * <refsect2 id="lifecycle">
 * <title>Lifecycle</title>
 * <para>
 * The typical use case for #IrisProgressMonitor is that you want for example
 * an #GtkIrisProgressInfoBar in your application's main window that displays
 * any slow processes that are currently in progress. If no processes are
 * running the info bar should be hidden. The recommended way to achieve this
 * is by creating one #GtkIrisProgressInfoBar object, during the initialization
 * of your program, and then showing and hiding this one widget appropriately.
 * You can get the widget to do this automatically by calling
 * iris_progress_monitor_set_permanent_mode(): the widget will then make sure
 * it is visible whenever it has any watches active, and will hide itself again
 * when the last process it is watching finishes.
 *
 * A more powerful mechanism exists if you need to go beyond this behaviour -
 * when all of the watches have completed, the #IrisProgressMonitor::finished
 * signal is emitted. You could, for example, connect this signal to
 * gtk_widget_destroy() to make the widget finalize itself when it has no more
 * active watches.
 * </para>
 * </refsect2>
 *
 * <refsect2 id="tasks">
 * <title>Watching Tasks</title>
 * <para>
 * The interface has the flexibility to watch any #IrisTask objects if desired.
 * To do this you must first call iris_progress_monitor_add_watch(). This
 * returns an #IrisPort object where you should then send messages of
 * #IrisProgressMessageType to update the UI. The progress monitor will watch
 * the task until it receives #IRIS_PROGRESS_MESSAGE_CANCELED or
 * #IRIS_PROGRESS_MESSAGE_COMPLETE. You can find an example implementation of
 * this in <filename>examples/progress-tasks</filename> in the iris source tree.
 * </para>
 * </refsect2>
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
	* #IrisProcess objects being watched are canceled automatically, but if
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


/* A group 'resets' when all its watches complete and it gets hidden */
void
_iris_progress_group_reset (IrisProgressGroup *group)
{
	group->canceled = FALSE;

	group->completed_watches = 0;
}

/**
 * iris_progress_monitor_add_group:
 * @progress_monitor: an #IrisProgressMonitor
 * @title: a name for the new group
 * @plural: generic description of the task (not currently used)
 *
 * Creates a new watch group. Groups enable you to group together related
 * processes or tasks; see <link linkend="grouping">the introduction</link>
 * for more information.
 *
 * The caller owns one reference on the returned object, which can be released
 * using iris_progress_group_unref().
 *
 * Returns: an #IrisProgressGroup object.
 *          
 **/
IrisProgressGroup *
iris_progress_monitor_add_group (IrisProgressMonitor *progress_monitor,
                                 const gchar         *title,
                                 const gchar         *plural)
{
	IrisProgressMonitorInterface *iface;
	IrisProgressGroup            *group;

	g_return_val_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor), 0);
	g_return_val_if_fail (title != NULL, 0);

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	group = g_slice_new0 (IrisProgressGroup);

	group->progress_monitor = progress_monitor;
	group->ref_count = 1;

	group->watch_list = NULL;

	group->title = g_strdup (title);
	group->plural = g_strdup (plural);

	group->progress_mode = IRIS_PROGRESS_CONTINUOUS;

	group->visible = FALSE;

	_iris_progress_group_reset (group);

	iface->add_group (progress_monitor, group);

	return group;
}


static IrisProgressWatch *
iris_progress_monitor_add_watch_internal (IrisProgressMonitor             *progress_monitor,
                                          IrisTask                        *task,
                                          const gchar                     *title,
                                          IrisProgressGroup               *group)
{
	IrisProgressMonitorInterface *iface;
	IrisProgressWatch *watch;

	watch = g_slice_new0 (IrisProgressWatch);
	watch->monitor = progress_monitor;
	watch->port = iris_port_new ();

	watch->progress_mode = iris_task_get_progress_mode (task);

	if (group != NULL) {
		iris_progress_group_ref (group);
		watch->group = group;

		group->watch_list = g_list_prepend (group->watch_list, watch);

		if (watch->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY)
			group->progress_mode = IRIS_PROGRESS_ACTIVITY_ONLY;
	}

	watch->title = g_strdup (title);

	watch->task = task;

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE(progress_monitor);
	iface->add_watch (progress_monitor, watch);

	return watch;
}

/* Stop receiving status messages from a watch that is still running */
void
_iris_progress_watch_disconnect (IrisProgressWatch *watch)
{
	/* The process/task being watched owns the port, we don't have any
	 * communication to it so it will just have to push useless messages
	 * to a disconnected port ... which is a waste, but shouldn't happen
	 * often because why close the dialog before the process is finished?
	 * FIXME: perhaps we could give ports a 'closed' status which would
	 * imply the watch has disconnected/whatever.
	 */
	iris_receiver_destroy (watch->receiver, NULL, FALSE);
}

void
_iris_progress_watch_free (IrisProgressWatch *watch)
{
	g_free (watch->title);

	if (watch->group != NULL) {
		watch->group->watch_list = g_list_remove (watch->group->watch_list,
		                                          watch);
		iris_progress_group_unref (watch->group);
		watch->group = NULL;

		/* We don't check if the group can stop displaying in activity only
		 * mode; either it was changed already when the watch completed, or the
		 * whole group got canceled so it makes no difference.
		 */
	}

	g_object_unref (watch->port);

	g_slice_free (IrisProgressWatch, watch);
}

/**
 * iris_progress_monitor_add_watch:
 * @progress_monitor: an #IrisProgressMonitor
 * @task: the #IrisTask that is being watched.
 * @title: name of the task being watched, used to label its progress bar, or
 *         %NULL.
 * @group: #IrisProgressGroup to add the watch to, or %NULL for none
 *
 * Causes @progress_monitor to add a new watch (generally represented by
 * a progress bar) for a task, which needs to update it by posting messages on
 * the #IrisPort that is returned. These messages should be from
 * #IrisProgressMessageType. The watch will be removed when the port receives
 * #IRIS_PROGRESS_MESSAGE_CANCELED or #IRIS_PROGRESS_MESSAGE_COMPLETE.
 *
 * See iris_progress_monitor_add_group() for more info on watch groups.
 *
 * Returns: an #IrisPort, listening for progress messages.
 */
IrisPort *
iris_progress_monitor_add_watch (IrisProgressMonitor             *progress_monitor,
                                 IrisTask                        *task,
                                 const gchar                     *title,
                                 IrisProgressGroup               *group)
{
	IrisProgressWatch            *watch;
	IrisProgressMonitorInterface *iface;

	g_return_val_if_fail (IRIS_IS_TASK (task), NULL);

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	if (iface->is_watching_task (progress_monitor, task))
		return NULL;

	watch = iris_progress_monitor_add_watch_internal
	          (progress_monitor, task, title, group);

	return watch->port;
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
 *             or process after it has completed or been canceled. Default
 *             value: 750 ms.
 *
 * It is slightly visually jarring for a process or group of processes that are
 * being watched to disappear instantly when they complete or the cancel button
 * is clicked. You should not normally need to change the default value of
 * 750ms, which allows time for the UI to catch up with the process and the
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
                        IrisProgressGroup               *group,
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
	           iris_process_get_title (process),
	           group);

	watch->chain_flag = chain;

	iris_process_add_watch (process, watch->port);

	return watch;
}


/**
 * iris_progress_monitor_watch_process:
 * @progress_monitor: an #IrisProgressMonitor
 * @process: an #IrisProcess
 * @group: group to add the process watch to, or 0 for none
 *
 * Visually display the progress of @process. If @process has source or sink
 * processes connected, they are ignored.
 *
 * See iris_progress_monitor_add_group() for more info on watch groups.
 **/
void
iris_progress_monitor_watch_process (IrisProgressMonitor             *progress_monitor,
                                     IrisProcess                     *process,
                                     IrisProgressGroup               *group)
{
	watch_process_internal (progress_monitor, process, group, FALSE);
}

/**
 * iris_progress_monitor_watch_process_chain:
 * @progress_monitor: an #IrisProgressMonitor
 * @process: an #IrisProcess
 * @title: overall title for this group of processes
 * @plural: a more general title
 *
 * Visually display the progress of @process, and any processes which are
 * connected. Each process in the chain is displayed separately in the order of
 * the data flow.
 *
 * This function will create a watch group for the chain of processes, which
 * improves the visual display. If you would like more control over what
 * happens, iris_progress_monitor_watch_process_chain_in_group() allows you to
 * add the chain to an existing group or avoid grouping the processes.
 * See <link linkend="grouping">the introduction</link> for more information on
 * groups.
 **/
void
iris_progress_monitor_watch_process_chain (IrisProgressMonitor             *progress_monitor,
                                           IrisProcess                     *process,
                                           const gchar                     *title,
                                           const gchar                     *plural)
{
	IrisProgressGroup *group;

	group = iris_progress_monitor_add_group (progress_monitor, title, plural);

	watch_process_internal (progress_monitor, process, group, TRUE);

	/* Drop the caller's reference; this means the object will be freed
	 * automatically whenever the process chain completes
	 */
	iris_progress_group_unref (group);
}

/**
 * iris_progress_monitor_watch_process_chain_in_group:
 * @progress_monitor: an #IrisProgressMonitor
 * @process: an #IrisProcess
 * @group: group in which to add @process and its linked processe, or 0.
 *
 * This function is a version of iris_progress_monitor_watch_process_chain()
 * which allows you to add @process and its linked processes to an existing
 * watch group, or to not add them to a group at all. See
 * <link linkend="grouping">the introduction</link> for more information.
 **/
void
iris_progress_monitor_watch_process_chain_in_group (IrisProgressMonitor             *progress_monitor,
                                                    IrisProcess                     *process,
                                                    IrisProgressGroup               *group)
{
	watch_process_internal (progress_monitor, process, group, TRUE);
}

/**************************************************************************
 *                       IrisProgressGroup stuff                          *
 *************************************************************************/

/**
 * iris_progress_group_ref:
 * @progress_group: an #IrisProgressGroup
 *
 * Increases the reference count of @progress_group.
 */
void
iris_progress_group_ref (IrisProgressGroup *progress_group) {
	g_atomic_int_inc (&progress_group->ref_count);
}

/**
 * iris_progress_group_unref:
 * @progress_group: an #IrisProgressGroup
 *
 * Removes a reference to @progress_group and frees the object if there are no
 * references remaining. Any watch which is a member of @progress_group will
 * also hold a reference, so @progress_group will never be freed until it is no
 * longer needed.
 */
void
iris_progress_group_unref (IrisProgressGroup *group) {
	IrisProgressMonitorInterface *iface;

	if (g_atomic_int_dec_and_test (&group->ref_count)) {
		g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (group->progress_monitor));

		/* Free user data (ie. the widgets) associated by the implementation */
		iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (group->progress_monitor);
		iface->remove_group (group->progress_monitor, group);

		group->progress_monitor = NULL;

		g_free (group->title);
		g_free (group->plural);

		g_slice_free (IrisProgressGroup, group);
	}
}


gboolean
_iris_progress_group_is_stopped (IrisProgressGroup *group)
{
	IrisProgressWatch *watch;
	GList             *node;

	for (node=group->watch_list; node; node=node->next) {
		watch = node->data;
		if (!watch->complete && !watch->canceled)
			return FALSE;
	}

	return TRUE;
}


/**************************************************************************
 *                    Widget implementation helpers                       *
 *************************************************************************/

void
_iris_progress_monitor_cancel_group   (IrisProgressMonitor *progress_monitor,
                                       IrisProgressGroup   *group)
{
	GList             *node;
	IrisProgressWatch *watch;

	for (node=group->watch_list; node; node=node->next) {
		watch = node->data;

		/* Allowed, it's possible with a long watch_hide_delay .. */
		if (watch->canceled || watch->complete)
			continue;

		iris_task_cancel (IRIS_TASK (watch->task));
	}

	g_signal_emit (progress_monitor, signals[CANCEL], 0);
}

void
_iris_progress_monitor_cancel_watch   (IrisProgressMonitor *progress_monitor,
                                       IrisProgressWatch   *watch)
{
	g_return_if_fail (!watch->canceled);

	iris_task_cancel (IRIS_TASK (watch->task));

	g_signal_emit (progress_monitor, signals[CANCEL], 0);
}

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
             IrisProcess                     *added_process)
{
	IrisProgressMonitorInterface *iface;
	IrisProcess                  *head,
	                             *process;
	IrisProgressWatch            *watch;
	IrisProgressGroup            *group;

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));
	g_return_if_fail (IRIS_IS_PROCESS (added_process));

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (progress_monitor);

	watch = iface->get_watch (progress_monitor, IRIS_TASK (added_process));
	group = watch->group;

	head = added_process;
	while (iris_process_has_predecessor (head))
		head = iris_process_get_predecessor (head);

	process = head;
	do {
		if (process == added_process && process != head) {
			/* User has been awkward and added the chain using a process that
			 * wasn't its first one
			 */
			iface->reorder_watch_in_group (progress_monitor,
			                               watch,
			                               TRUE);
		} else
			watch_process_internal (progress_monitor, 
			                        process,
			                        group,
			                        FALSE);
	} while ((process = iris_process_get_successor (process)) != NULL);
}

static void
handle_canceled (IrisProgressWatch *watch,
                  IrisMessage *message)
{
	watch->canceled = TRUE;
}

static void
handle_complete (IrisProgressWatch *completed_watch,
                 IrisMessage       *message)
{
	IrisProgressWatch *watch;
	IrisProgressGroup *group;
	GList             *node;
	gboolean           need_activity_only_mode;

	completed_watch->complete = TRUE;

	group = completed_watch->group;
	if (group != NULL) {
		/* Can we take the group out of activity-only display mode? */
		need_activity_only_mode = FALSE;

		for (node=group->watch_list; node; node=node->next) {
			watch = node->data;
			if (watch->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY &&
			    !watch->complete) {
				need_activity_only_mode = TRUE;
				break;
			}
		}

		if (need_activity_only_mode)
			group->progress_mode = IRIS_PROGRESS_ACTIVITY_ONLY;
		else
			group->progress_mode = IRIS_PROGRESS_CONTINUOUS;
	}
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

	if (watch->canceled || watch->complete)
		if (message->what != IRIS_PROGRESS_MESSAGE_TITLE)
			g_warning ("IrisProgressMonitor: watch %lx sent a progress message "
			           "after already sending %s.\n",
			           (gulong)watch,
			           watch->canceled? "CANCELED": "COMPLETE");

	g_return_if_fail (IRIS_IS_PROGRESS_MONITOR (progress_monitor));

	/* Any of these messages indicate that the connections cannot now change. Note that for
	 * watch->chain_flag to be TRUE if watch->task is not of type IrisProcess is an error. 
	 * Normally you must be aware that watch->task could be an IrisTask.
	 */
	if (watch->chain_flag) {
		watch->chain_flag = FALSE;
		watch_chain (progress_monitor, IRIS_PROCESS (watch->task));
	}

	switch (message->what) {
		case IRIS_PROGRESS_MESSAGE_CANCELED:
			handle_canceled (watch, message);
			break;
		case IRIS_PROGRESS_MESSAGE_COMPLETE:
			handle_complete (watch, message);
			break;
		case IRIS_PROGRESS_MESSAGE_PULSE:
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

static void
make_progress_string (IrisProgressMode  progress_mode,
                      gdouble                          fraction,
                      gint                             processed_items,
                      gint                             total_items,
                      gchar                           *p_progress_text)
{
	switch (progress_mode) {
		case IRIS_PROGRESS_DISCRETE:
			g_snprintf (p_progress_text, 255, _("%i items of %i"), 
			            processed_items, total_items);
			break;
		case IRIS_PROGRESS_CONTINUOUS:
			g_snprintf (p_progress_text, 255, _("%.0f%% complete"), 
			            (fraction * 100.0));
			break;
		default:
			g_warning ("iris-progress-monitor: Invalid progress_mode.\n");
			p_progress_text[0] = 0;
	}
}

void
_iris_progress_monitor_format_watch_progress (IrisProgressMonitor *progress_monitor,
                                              IrisProgressWatch   *watch,
                                              gchar               *p_progress_text)
{
	if (watch->complete) {
		g_snprintf (p_progress_text, 255, _("Complete"));
		return;
	} else if (watch->canceled) {
		g_snprintf (p_progress_text, 255, _("Cancelled"));
		return;
	}

	make_progress_string (watch->progress_mode, watch->fraction,
	                      watch->processed_items, watch->total_items,
	                      p_progress_text);
}

void
_iris_progress_monitor_format_group_progress (IrisProgressMonitor *progress_monitor,
                                              IrisProgressGroup   *group,
                                              gchar               *p_progress_text,
                                              gdouble             *p_fraction)
{
	IrisProgressWatch *watch;
	GList   *node;
	gboolean complete = TRUE;
	gdouble  fraction = 0;
	gint     watch_count = 0;

	g_return_if_fail (group->watch_list != NULL);

	if (group->canceled) {
		g_snprintf (p_progress_text, 255, _("Cancelled"));
		return;
	}

	for (node=group->watch_list; node; node=node->next) {
		watch = node->data;

		/* We do count completed watches that are in the list, because they are
		 * only accounted for in ->completed_watches once they are removed from
		 * the list
		 */
		if (!watch->complete)
			complete = FALSE;

		fraction += watch->fraction;
		watch_count ++;
	}

	fraction += group->completed_watches;
	fraction /= (group->completed_watches + watch_count);

	if (p_fraction != NULL)
		*p_fraction = fraction;

	if (complete) {
		g_snprintf (p_progress_text, 255, _("Complete"));
		return;
	}

	make_progress_string (IRIS_PROGRESS_CONTINUOUS, fraction,
	                      0, 0, p_progress_text);
}
