/* iris-process-dialog.h
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

#ifndef __IRIS_PROGRESS_DIALOG_H__
#define __IRIS_PROGRESS_DIALOG_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "iris-progress-monitor.h"

G_BEGIN_DECLS

#define IRIS_TYPE_PROGRESS_DIALOG            (iris_progress_dialog_get_type ())
#define IRIS_PROGRESS_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_PROGRESS_DIALOG, IrisProgressDialog))
#define IRIS_PROGRESS_DIALOG_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_PROGRESS_DIALOG, IrisProgressDialog const))
#define IRIS_PROGRESS_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_PROGRESS_DIALOG, IrisProgressDialogClass))
#define IRIS_IS_PROGRESS_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_PROGRESS_DIALOG))
#define IRIS_IS_PROGRESS_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_PROGRESS_DIALOG))
#define IRIS_PROGRESS_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_PROGRESS_DIALOG, IrisProgressDialogClass))

typedef struct _IrisProgressDialog        IrisProgressDialog;
typedef struct _IrisProgressDialogClass   IrisProgressDialogClass;
typedef struct _IrisProgressDialogPrivate IrisProgressDialogPrivate;

struct _IrisProgressDialog
{
	GtkDialog parent;

	/*< private >*/
	IrisProgressDialogPrivate *priv;
};

struct _IrisProgressDialogClass
{
	GtkDialogClass parent_class;

	void     (*reserved1)           (void);
	void     (*reserved2)           (void);
	void     (*reserved3)           (void);
	void     (*reserved4)           (void);
};

GType               iris_progress_dialog_get_type             (void) G_GNUC_CONST;

GtkWidget          *iris_progress_dialog_new                  (const gchar *title,
                                                               GtkWindow   *parent);

G_END_DECLS

#endif /* __IRIS_PROGRESS_DIALOG_H__ */
