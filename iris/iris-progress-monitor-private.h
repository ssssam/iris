/* iris-progress-monitor-private.h
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

#ifndef __IRIS_PROGRESS_MONITOR_PRIVATE_H__
#define __IRIS_PROGRESS_MONITOR_PRIVATE_H__

G_BEGIN_DECLS

struct _IrisProgressGroup
{
	IrisProgressMonitor *progress_monitor;

	volatile gint ref_count;

	GList *watch_list;

	gchar *title,
	      *plural;

	/* Can't be ITEMS, that doesn't make sense for overall progress */
	IrisProgressMonitorDisplayStyle display_style;

	/* Used to calculate total progress */
	gint   completed_watches;

	guint  visible : 1;
	guint  cancelled : 1;

	/* User data */
	gpointer toplevel,
	         watch_box,
	         progress_bar,
	         cancel_widget,
	         user_data1,
	         user_data2,
	         user_data3;
};

struct _IrisProgressWatch
{
	IrisProgressMonitor *monitor;
	IrisPort            *port;

	IrisReceiver        *receiver;

	guint cancelled: 1;
	guint complete: 1;

	/* Until the process is running, its connections can change. Rather than
	 * monitor this, if all chained processes are to be watched we wait until
	 * this process executes to add them so they cannot change. This flag marks
	 * if we need to add watches for connected processes when this is possible.
	 */
	guint chain_flag: 1;

	IrisProgressMonitorDisplayStyle display_style;

	/* These three are set by the interface in
	 * iris_progress_monitor_update_watch() and
	 * iris_progress_monitor_update_watch_items(). */
	gint  processed_items, total_items;
	float fraction;

	IrisProgressGroup *group;
	gchar *title;

	/* This member should be used by implementations to ensure the same process
	 * is not watched multiple times.
	 */
	IrisTask *task;

	/* For implementations to store g_source that will hide the watch. */
	gint finish_timeout_id;

	/* For use by implementations to store widget pointers etc. */
	gpointer toplevel,
	         title_label,
	         progress_bar,
	         cancel_widget,
	         user_data1,
	         user_data2;
};


void _iris_progress_watch_free             (IrisProgressWatch *watch);
void _iris_progress_watch_disconnect       (IrisProgressWatch *watch);

void _iris_progress_group_reset            (IrisProgressGroup *group);

gboolean _iris_progress_group_is_stopped   (IrisProgressGroup *group);

/* Callback for cancel buttons in the dialogs */
void _iris_progress_monitor_cancel_group   (IrisProgressMonitor *progress_monitor,
                                            IrisProgressGroup   *group);
void _iris_progress_monitor_cancel_watch   (IrisProgressMonitor *progress_monitor,
                                            IrisProgressWatch   *watch);

/* Emit IrisProgressMonitor::finished */
void _iris_progress_monitor_finished       (IrisProgressMonitor *progress_monitor);

/* Format status of watch/group into 'progress_text', as for example x% or a/b items.
 * progress_text must point to a character array of 256 bytes or more. */
void _iris_progress_monitor_format_watch_progress   (IrisProgressMonitor *progress_monitor,
                                                     IrisProgressWatch   *watch,
                                                     gchar               *p_progress_text);
void _iris_progress_monitor_format_group_progress   (IrisProgressMonitor *progress_monitor,
                                                     IrisProgressGroup   *group,
                                                     gchar               *p_progress_text,
                                                     gdouble             *p_fraction);

/* This function should be the handler for the watch's progress messages.
 * It invokes the class handler after updating the watch object.
 */
void _iris_progress_monitor_handle_message (IrisMessage       *message,
                                            gpointer           user_data);

G_END_DECLS

#endif /* __IRIS_PROGRESS_MONITOR_PRIVATE_H__ */
