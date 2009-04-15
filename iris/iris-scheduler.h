/* iris-scheduler.h
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

#ifndef __IRIS_SCHEDULER_H__
#define __IRIS_SCHEDULER_H__

#include <glib-object.h>

#include "iris-types.h"
#include "iris-thread.h"

G_BEGIN_DECLS

#define IRIS_TYPE_SCHEDULER (iris_scheduler_get_type ())

#define IRIS_SCHEDULER(obj)                  \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),      \
     IRIS_TYPE_SCHEDULER, IrisScheduler))

#define IRIS_SCHEDULER_CONST(obj)            \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),      \
     IRIS_TYPE_SCHEDULER, IrisScheduler const))

#define IRIS_SCHEDULER_CLASS(klass)          \
    (G_TYPE_CHECK_CLASS_CAST ((klass),       \
     IRIS_TYPE_SCHEDULER, IrisSchedulerClass))

#define IRIS_IS_SCHEDULER(obj)               \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj),      \
     IRIS_TYPE_SCHEDULER))

#define IRIS_IS_SCHEDULER_CLASS(klass)       \
    (G_TYPE_CHECK_CLASS_TYPE ((klass),       \
     IRIS_TYPE_SCHEDULER))

#define IRIS_SCHEDULER_GET_CLASS(obj)        \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),       \
     IRIS_TYPE_SCHEDULER, IrisSchedulerClass))

struct _IrisScheduler
{
	GObject parent;

	/*< private >*/
	IrisSchedulerPrivate *priv;
	gboolean              maxed;
};

struct _IrisSchedulerClass
{
	GObjectClass  parent_class;

	gint  (*get_max_threads) (IrisScheduler  *scheduler);
	gint  (*get_min_threads) (IrisScheduler  *scheduler);

	void  (*queue)           (IrisScheduler  *scheduler,
	                          IrisCallback    func,
	                          gpointer        data,
	                          GDestroyNotify  notify);

	void (*add_thread)       (IrisScheduler  *scheduler,
	                          IrisThread     *thread);
	void (*remove_thread)    (IrisScheduler  *scheduler,
	                          IrisThread     *thread);
};

GType          iris_scheduler_get_type        (void) G_GNUC_CONST;

IrisScheduler* iris_scheduler_new             (void);
IrisScheduler* iris_scheduler_new_full        (guint           min_threads,
                                               guint           max_threads);
IrisScheduler* iris_scheduler_default         (void);

gint           iris_scheduler_get_min_threads (IrisScheduler  *scheduler);
gint           iris_scheduler_get_max_threads (IrisScheduler  *scheduler);
void           iris_scheduler_queue           (IrisScheduler  *scheduler,
                                               IrisCallback    func,
                                               gpointer        data,
                                               GDestroyNotify  notify);

void           iris_scheduler_add_thread      (IrisScheduler  *scheduler,
                                               IrisThread     *thread);
void           iris_scheduler_remove_thread   (IrisScheduler  *scheduler,
                                               IrisThread     *thread);

G_END_DECLS

#endif /* __IRIS_SCHEDULER_H__ */
