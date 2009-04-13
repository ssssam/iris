/* iris-wswsscheduler.h
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

#ifndef __IRIS_WSSCHEDULER_H__
#define __IRIS_WSSCHEDULER_H__

#include <glib-object.h>

#include "iris-types.h"
#include "iris-scheduler.h"

G_BEGIN_DECLS

#define IRIS_TYPE_WSSCHEDULER (iris_wsscheduler_get_type ())

#define IRIS_WSSCHEDULER(obj)                      \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),            \
     IRIS_TYPE_WSSCHEDULER, IrisWSScheduler))

#define IRIS_WSSCHEDULER_CONST(obj)                \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),            \
     IRIS_TYPE_WSSCHEDULER, IrisWSScheduler const))

#define IRIS_WSSCHEDULER_CLASS(klass)              \
    (G_TYPE_CHECK_CLASS_CAST ((klass),             \
     IRIS_TYPE_WSSCHEDULER, IrisWSSchedulerClass))

#define IRIS_IS_WSSCHEDULER(obj)                   \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj),            \
     IRIS_TYPE_WSSCHEDULER))

#define IRIS_IS_WSSCHEDULER_CLASS(klass)           \
    (G_TYPE_CHECK_CLASS_TYPE ((klass),             \
     IRIS_TYPE_WSSCHEDULER))

#define IRIS_WSSCHEDULER_GET_CLASS(obj)            \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),             \
     IRIS_TYPE_WSSCHEDULER, IrisWSSchedulerClass))

struct _IrisWSScheduler
{
	IrisScheduler parent;

	/*< private >*/
	IrisWSSchedulerPrivate *priv;
};

struct _IrisWSSchedulerClass
{
	IrisSchedulerClass parent_class;
};

GType          iris_wsscheduler_get_type        (void) G_GNUC_CONST;
IrisScheduler* iris_wsscheduler_new             (void);
IrisScheduler* iris_wsscheduler_new_full        (guint min_threads,
                                                 guint max_threads);

G_END_DECLS

#endif /* __IRIS_WSSCHEDULER_H__ */
