/* iris-progress-dialog-private.h
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

#ifndef __GTK_IRIS_PROGRESS_DIALOG_PRIVATE_H__
#define __GTK_IRIS_PROGRESS_DIALOG_PRIVATE_H__

G_BEGIN_DECLS

#include "iris-scheduler.h"
#include "iris-progress-monitor-private.h"

#define GTK_IRIS_PROGRESS_DIALOG_GET_PRIVATE(object)                  \
          (G_TYPE_INSTANCE_GET_PRIVATE((object),                  \
           GTK_IRIS_TYPE_PROGRESS_DIALOG, GtkIrisProgressDialogPrivate))

struct _GtkIrisProgressDialogPrivate
{
	IrisScheduler *scheduler;

	GList         *watch_list;

	GtkWidget     *box;
	gchar         *title_format;        /* NULL if default title to be used */
	gint           max_width;

	gint           watch_hide_delay;

	guint          permanent_mode : 1;

	guint          in_finished : 1;     /* TRUE when emitting ::finished */
	guint          in_destruction : 1;  /* TRUE from dispose or finalize handlers */
	guint          title_is_static : 1; /* TRUE if title_format does not include '%s' */
};

G_END_DECLS

#endif /* __GTK_IRIS_PROGRESS_DIALOG_PRIVATE_H__ */
