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

#ifdef LINUX
#include <sys/sysinfo.h>
#endif

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

guint
get_n_cpu (void)
{
#ifdef Linux
	return get_nprocs ();
#else
	/* FIXME: Support more than Linux. */
	return 1;
#endif
}

static guint
iris_scheduler_get_min_threads_real (IrisScheduler *scheduler)
{
	/* min-thread of 0 means the scheduler can yield all of
	 * its threads until it needs one. at which point it needs
	 * to request a thread from the manager.
	 */
	return 0;
}

static guint
iris_scheduler_get_max_threads_real (IrisScheduler *scheduler)
{
	/* A max threads of 0 means unlimited. By default, we ask
	 * for no more than n_cpu. If there is only one cpu, we
	 * will default to 2.
	 */
	guint n_cpu       = get_n_cpu ();
	guint max_threads = 0;

	if (n_cpu == 1)
		max_threads = 2;
	else
		max_threads = n_cpu;

	g_assert (max_threads > 0);

	return max_threads;
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
	klass->get_min_threads = iris_scheduler_get_min_threads_real;
	klass->get_max_threads = iris_scheduler_get_max_threads_real;

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
