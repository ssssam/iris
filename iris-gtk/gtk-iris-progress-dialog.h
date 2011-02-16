/* iris-process-dialog.h
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

#ifndef __GTK_IRIS_PROGRESS_DIALOG_H__
#define __GTK_IRIS_PROGRESS_DIALOG_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "iris-progress-monitor.h"

G_BEGIN_DECLS

#define GTK_IRIS_TYPE_PROGRESS_DIALOG            (gtk_iris_progress_dialog_get_type ())
#define GTK_IRIS_PROGRESS_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_IRIS_TYPE_PROGRESS_DIALOG, GtkIrisProgressDialog))
#define GTK_IRIS_PROGRESS_DIALOG_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_IRIS_TYPE_PROGRESS_DIALOG, GtkIrisProgressDialog const))
#define GTK_IRIS_PROGRESS_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GTK_IRIS_TYPE_PROGRESS_DIALOG, GtkIrisProgressDialogClass))
#define GTK_IRIS_IS_PROGRESS_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_IRIS_TYPE_PROGRESS_DIALOG))
#define GTK_IRIS_IS_PROGRESS_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GTK_IRIS_TYPE_PROGRESS_DIALOG))
#define GTK_IRIS_PROGRESS_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GTK_IRIS_TYPE_PROGRESS_DIALOG, GtkIrisProgressDialogClass))

typedef struct _GtkIrisProgressDialog        GtkIrisProgressDialog;
typedef struct _GtkIrisProgressDialogClass   GtkIrisProgressDialogClass;
typedef struct _GtkIrisProgressDialogPrivate GtkIrisProgressDialogPrivate;

struct _GtkIrisProgressDialog
{
	GtkDialog parent;

	/*< private >*/
	GtkIrisProgressDialogPrivate *priv;
};

struct _GtkIrisProgressDialogClass
{
	GtkDialogClass parent_class;

	void     (*reserved1)           (void);
	void     (*reserved2)           (void);
	void     (*reserved3)           (void);
	void     (*reserved4)           (void);
};

GType      gtk_iris_progress_dialog_get_type  (void) G_GNUC_CONST;

GtkWidget *gtk_iris_progress_dialog_new       (GtkWindow          *parent);

void       gtk_iris_progress_dialog_set_title (GtkIrisProgressDialog *progress_dialog,
                                           const gchar        *title_format);

G_END_DECLS

#endif /* __GTK_IRIS_PROGRESS_DIALOG_H__ */
