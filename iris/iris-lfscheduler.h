/* iris-lfscheduler.h
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

#ifndef __IRIS_LFSCHEDULER_H__
#define __IRIS_LFSCHEDULER_H__

#include <glib-object.h>

#include "iris-scheduler.h"

G_BEGIN_DECLS

#define IRIS_TYPE_LFSCHEDULER            (iris_lfscheduler_get_type ())
#define IRIS_LFSCHEDULER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_LFSCHEDULER, IrisLFScheduler))
#define IRIS_LFSCHEDULER_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_LFSCHEDULER, IrisLFScheduler const))
#define IRIS_LFSCHEDULER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_LFSCHEDULER, IrisLFSchedulerClass))
#define IRIS_IS_LFSCHEDULER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_LFSCHEDULER))
#define IRIS_IS_LFSCHEDULER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_LFSCHEDULER))
#define IRIS_LFSCHEDULER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_LFSCHEDULER, IrisLFSchedulerClass))

typedef struct _IrisLFScheduler           IrisLFScheduler;
typedef struct _IrisLFSchedulerClass      IrisLFSchedulerClass;
typedef struct _IrisLFSchedulerPrivate    IrisLFSchedulerPrivate;

struct _IrisLFScheduler
{
	IrisScheduler parent;

	/*< private >*/
	IrisLFSchedulerPrivate *priv;
};

struct _IrisLFSchedulerClass
{
	IrisSchedulerClass parent_class;
};

GType          iris_lfscheduler_get_type (void) G_GNUC_CONST;
IrisScheduler* iris_lfscheduler_new      (void);
IrisScheduler* iris_lfscheduler_new_full (guint min_threads,
                                          guint max_threads);

G_END_DECLS

#endif /* __IRIS_LFSCHEDULER_H__ */
