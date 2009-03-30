/* iris-scheduler.c
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

#include "iris-scheduler.h"
#include "iris-scheduler-private.h"

struct _IrisSchedulerPrivate
{
	gpointer dummy;
};

G_DEFINE_TYPE (IrisScheduler, iris_scheduler, G_TYPE_OBJECT);

static void
iris_scheduler_queue_real (IrisScheduler     *scheduler,
                           IrisSchedulerFunc  func,
                           gpointer           data,
                           GDestroyNotify     notify)
{
	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));
	g_return_if_fail (func != NULL);

	// FIXME: Just running synchronously until we get further along in hacking

	func (data);

	if (notify)
		notify (data);
}

static void
iris_scheduler_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_scheduler_parent_class)->finalize (object);
}

static void
iris_scheduler_class_init (IrisSchedulerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->queue = iris_scheduler_queue_real;
	object_class->finalize = iris_scheduler_finalize;

	g_type_class_add_private (object_class, sizeof (IrisSchedulerPrivate));
}

static void
iris_scheduler_init (IrisScheduler *scheduler)
{
	scheduler->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheduler,
	                                          IRIS_TYPE_SCHEDULER,
	                                          IrisSchedulerPrivate);
}

IrisScheduler*
iris_scheduler_new (void)
{
	return g_object_new (IRIS_TYPE_SCHEDULER, NULL);
}

void
iris_scheduler_queue (IrisScheduler     *scheduler,
                      IrisSchedulerFunc  func,
                      gpointer           data,
                      GDestroyNotify     notify)
{
	IRIS_SCHEDULER_GET_CLASS (scheduler)->queue (scheduler, func, data, notify);
}
