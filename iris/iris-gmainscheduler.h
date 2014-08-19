/* iris-gmainscheduler.h
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

#ifndef __IRIS_GMAINSCHEDULER_H__
#define __IRIS_GMAINSCHEDULER_H__

#include <glib-object.h>

#include "iris-scheduler.h"

G_BEGIN_DECLS

#define IRIS_TYPE_GMAINSCHEDULER            (iris_gmainscheduler_get_type ())
#define IRIS_GMAINSCHEDULER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_GMAINSCHEDULER, IrisGMainScheduler))
#define IRIS_GMAINSCHEDULER_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_GMAINSCHEDULER, IrisGMainScheduler const))
#define IRIS_GMAINSCHEDULER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_GMAINSCHEDULER, IrisGMainSchedulerClass))
#define IRIS_IS_GMAINSCHEDULER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_GMAINSCHEDULER))
#define IRIS_IS_GMAINSCHEDULER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_GMAINSCHEDULER))
#define IRIS_GMAINSCHEDULER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_GMAINSCHEDULER, IrisGMainSchedulerClass))

typedef struct _IrisGMainScheduler        IrisGMainScheduler;
typedef struct _IrisGMainSchedulerClass   IrisGMainSchedulerClass;
typedef struct _IrisGMainSchedulerPrivate IrisGMainSchedulerPrivate;

struct _IrisGMainScheduler
{
	IrisScheduler parent;

	/*< private >*/
	IrisGMainSchedulerPrivate *priv;
};

struct _IrisGMainSchedulerClass
{
	IrisSchedulerClass parent_class;
};

GType          iris_gmainscheduler_get_type    (void) G_GNUC_CONST;
IrisScheduler* iris_gmainscheduler_new         (GMainContext       *context);
GMainContext*  iris_gmainscheduler_get_context (IrisGMainScheduler *gmain_scheduler);

G_END_DECLS

#endif /* __IRIS_GMAINSCHEDULER_H__ */
