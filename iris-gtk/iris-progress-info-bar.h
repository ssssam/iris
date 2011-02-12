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

#ifndef __IRIS_PROGRESS_INFO_BAR_H__
#define __IRIS_PROGRESS_INFO_BAR_H__

#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "iris-progress-monitor.h"

G_BEGIN_DECLS

#define IRIS_TYPE_PROGRESS_INFO_BAR            (iris_progress_info_bar_get_type ())
#define IRIS_PROGRESS_INFO_BAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_PROGRESS_INFO_BAR, IrisProgressInfoBar))
#define IRIS_PROGRESS_INFO_BAR_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_PROGRESS_INFO_BAR, IrisProgressInfoBar const))
#define IRIS_PROGRESS_INFO_BAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_PROGRESS_INFO_BAR, IrisProgressInfoBarClass))
#define IRIS_IS_PROGRESS_INFO_BAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_PROGRESS_INFO_BAR))
#define IRIS_IS_PROGRESS_INFO_BAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_PROGRESS_INFO_BAR))
#define IRIS_PROGRESS_INFO_BAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_PROGRESS_INFO_BAR, IrisProgressInfoBarClass))

typedef struct _IrisProgressInfoBar        IrisProgressInfoBar;
typedef struct _IrisProgressInfoBarClass   IrisProgressInfoBarClass;
typedef struct _IrisProgressInfoBarPrivate IrisProgressInfoBarPrivate;

struct _IrisProgressInfoBar
{
	GtkInfoBar parent;

	/*< private >*/
	IrisProgressInfoBarPrivate *priv;
};

struct _IrisProgressInfoBarClass
{
	GtkInfoBarClass parent_class;

	void     (*reserved1)           (void);
	void     (*reserved2)           (void);
	void     (*reserved3)           (void);
	void     (*reserved4)           (void);
};

GType               iris_progress_info_bar_get_type             (void) G_GNUC_CONST;

GtkWidget          *iris_progress_info_bar_new                  ();

G_END_DECLS

#endif /* __IRIS_PROGRESS_INFO_BAR_H__ */
