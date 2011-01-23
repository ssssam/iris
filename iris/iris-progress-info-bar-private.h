/* iris-progress-info-bar-private.h
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

#ifndef __IRIS_PROGRESS_INFO_BAR_PRIVATE_H__
#define __IRIS_PROGRESS_INFO_BAR_PRIVATE_H__

G_BEGIN_DECLS

#include "iris-scheduler.h"
#include "iris-progress-monitor-private.h"

#define IRIS_PROGRESS_INFO_BAR_GET_PRIVATE(object)                  \
          (G_TYPE_INSTANCE_GET_PRIVATE((object),                  \
           IRIS_TYPE_PROGRESS_INFO_BAR, IrisProgressInfoBarPrivate))

struct _IrisProgressInfoBarPrivate
{
	IrisScheduler *scheduler;

	GList *watch_list;

	GtkWidget *title_label,
	          *total_progress_bar,
	          *watch_vbox;

	guint      in_finished : 1;
	guint      permanent_mode : 1;

	gint       watch_hide_delay;
};

/* For testing */
IrisProgressWatch *_iris_progress_info_bar_get_watch (IrisProgressInfoBar *progress_info_bar,
                                                      IrisTask            *task);

G_END_DECLS

#endif /* __IRIS_PROGRESS_INFO_BAR_PRIVATE_H__ */
