/* iris-scheduler-private.h
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

#ifndef __IRIS_SCHEDULER_PRIVATE_H__
#define __IRIS_SCHEDULER_PRIVATE_H__

#include "iris-rrobin.h"

G_BEGIN_DECLS

struct _IrisSchedulerPrivate
{
	GMutex      *mutex;        /* Synchronization for setting up the
	                            * scheduler instance.  Provides for lazy
	                            * instantiation.
	                            */

	IrisRRobin  *rrobin;       /* Round robin of per-thread queues used
	                            * by threads for work-stealing.
	                            */

	GAsyncQueue *queue;        /* Global Queue, used by work items
	                            * not originating from a thread within
	                            * the scheduler.
	                            */

	/* FIXME: Should we push these items into another cache-line so
	 *        they do not get nuked from the synchronizations above.
	 */

	guint             min_threads;
	guint             max_threads;
	volatile gint     has_leader;
	volatile gint     initialized;
};

IrisScheduler* iris_scheduler_new         (void);

G_END_DECLS

#endif /* __IRIS_SCHEDULER_PRIVATE_H__ */
