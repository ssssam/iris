/* iris-progress-monitor-private.h
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

#ifndef __IRIS_PROGRESS_MONITOR_PRIVATE_H__
#define __IRIS_PROGRESS_MONITOR_PRIVATE_H__

G_BEGIN_DECLS

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

	gchar *title;

	/* This member should be used by implementations to ensure the same process
	 * is not watched multiple times. */
	IrisTask *task;

	/* For use by implementations */
	gpointer user_data,
	         user_data2,
	         user_data3;
};

void               _iris_progress_watch_free        (IrisProgressWatch *watch);

/* Return TRUE if a progress monitor implementation can close, based on the
 * list of activities it is watching.
 */
gboolean _iris_progress_monitor_watch_list_finished (GList *watch_list);

/* Called when cancel button pressed on widget - will cancel every process in 
 * watch_list and emit 'cancel' signal.
 */
void     _iris_progress_monitor_cancel              (IrisProgressMonitor *progress_monitor,
                                                     GList               *watch_list);

/* Format status of watch into 'progress_text', as for example x% or a/b items.
 * progress_text must point to a character array of 256 bytes or more. */
void     _iris_progress_monitor_format_watch        (IrisProgressMonitor *progress_monitor,
                                                     IrisProgressWatch   *watch,
                                                     gchar               *progress_text);

/* This function should be the handler for the watch's progress messages.
 * It invokes the class handler after updating the watch object.
 */
void     _iris_progress_monitor_handle_message      (IrisMessage       *message,
                                                     gpointer           user_data);

G_END_DECLS

#endif /* __IRIS_PROGRESS_MONITOR_PRIVATE_H__ */
