/* iris-task.h
 *
 * Copyright (C) 2009 Christian Hergert <chris@dronelabs.com>
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

#ifndef __IRIS_TASK_H__
#define __IRIS_TASK_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define IRIS_TYPE_TASK             (iris_task_get_type ())
#define IRIS_TASK(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_TASK, IrisTask))
#define IRIS_TASK_CONST(obj)       (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_TASK, IrisTask const))
#define IRIS_TASK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_TASK, IrisTaskClass))
#define IRIS_IS_TASK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_TASK))
#define IRIS_IS_TASK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_TASK))
#define IRIS_TASK_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_TASK, IrisTaskClass))

typedef struct _IrisTask        IrisTask;
typedef struct _IrisTaskClass   IrisTaskClass;
typedef struct _IrisTaskPrivate IrisTaskPrivate;

struct _IrisTask
{
	GObject parent;

	/*< private >*/
	IrisTaskPrivate *priv;
};

struct _IrisTaskClass
{
	GObjectClass parent_class;
};

GType     iris_task_get_type (void) G_GNUC_CONST;
IrisTask* iris_task_new      (void);

G_END_DECLS

#endif /* __IRIS_TASK_H__ */
