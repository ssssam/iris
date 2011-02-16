/* iris-process-info-bar.h
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

#ifndef __GTK_IRIS_PROGRESS_INFO_BAR_H__
#define __GTK_IRIS_PROGRESS_INFO_BAR_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "iris-progress-monitor.h"

G_BEGIN_DECLS

#define GTK_IRIS_TYPE_PROGRESS_INFO_BAR            (gtk_iris_progress_info_bar_get_type ())
#define GTK_IRIS_PROGRESS_INFO_BAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_IRIS_TYPE_PROGRESS_INFO_BAR, GtkIrisProgressInfoBar))
#define GTK_IRIS_PROGRESS_INFO_BAR_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_IRIS_TYPE_PROGRESS_INFO_BAR, GtkIrisProgressInfoBar const))
#define GTK_IRIS_PROGRESS_INFO_BAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GTK_IRIS_TYPE_PROGRESS_INFO_BAR, GtkIrisProgressInfoBarClass))
#define GTK_IRIS_IS_PROGRESS_INFO_BAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_IRIS_TYPE_PROGRESS_INFO_BAR))
#define GTK_IRIS_IS_PROGRESS_INFO_BAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GTK_IRIS_TYPE_PROGRESS_INFO_BAR))
#define GTK_IRIS_PROGRESS_INFO_BAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GTK_IRIS_TYPE_PROGRESS_INFO_BAR, GtkIrisProgressInfoBarClass))

typedef struct _GtkIrisProgressInfoBar        GtkIrisProgressInfoBar;
typedef struct _GtkIrisProgressInfoBarClass   GtkIrisProgressInfoBarClass;
typedef struct _GtkIrisProgressInfoBarPrivate GtkIrisProgressInfoBarPrivate;

struct _GtkIrisProgressInfoBar
{
	GtkInfoBar parent;

	/*< private >*/
	GtkIrisProgressInfoBarPrivate *priv;
};

struct _GtkIrisProgressInfoBarClass
{
	GtkInfoBarClass parent_class;

	void     (*reserved1)           (void);
	void     (*reserved2)           (void);
	void     (*reserved3)           (void);
	void     (*reserved4)           (void);
};

GType               gtk_iris_progress_info_bar_get_type             (void) G_GNUC_CONST;

GtkWidget          *gtk_iris_progress_info_bar_new                  ();

G_END_DECLS

#endif /* __GTK_IRIS_PROGRESS_INFO_BAR_H__ */
