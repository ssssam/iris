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

	guint cancelled: 1;
	guint complete: 1;

	/* These three are set by the interface in
	 * iris_progress_monitor_update_watch () */
	gint  processed_items, total_items;
	float fraction;

	gchar *title;

	/* This member should be used by implementations to ensure the same process
	 * is not watched multiple times. */
	IrisProcess *process;

	/* For use by implementations */
	gpointer user_data,
	         user_data2,
	         user_data3;
};

void     _iris_progress_watch_free                  (IrisProgressWatch *watch);

/* Return TRUE if a progress monitor implementation can close, based on the
 * list of activities it is watching.
 */
gboolean _iris_progress_monitor_watch_list_finished (GList *watch_list);

/* Called when cancel button pressed on widget - will cancel every process in 
 * watch_list and emit 'cancel' signal.
 */
void     _iris_progress_monitor_cancel              (IrisProgressMonitor *progress_monitor,
                                                     GList               *process_watch_list);

G_END_DECLS

#endif /* __IRIS_PROGRESS_MONITOR_PRIVATE_H__ */
