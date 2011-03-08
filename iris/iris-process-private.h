/* iris-process-private.h
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

#ifndef __IRIS_PROCESS_PRIVATE_H__
#define __IRIS_PROCESS_PRIVATE_H__

#include <glib-object.h>

/*#include "iris-gmainscheduler.h"*/
#include "iris-port.h"
#include "iris-receiver.h"
#include "iris-lfqueue.h"
#include "iris-task-private.h"

#define IRIS_PROCESS_GET_PRIVATE(object)       \
	(G_TYPE_INSTANCE_GET_PRIVATE((object),     \
	 IRIS_TYPE_PROCESS, IrisProcessPrivate))

typedef enum
{
	IRIS_PROCESS_FLAG_OPEN       = 1 << 10,

	IRIS_PROCESS_FLAG_HAS_SOURCE = 1 << 11,
	IRIS_PROCESS_FLAG_HAS_SINK   = 1 << 12,
} IrisProcessFlags;

typedef enum
{
	IRIS_PROCESS_MESSAGE_CLOSE = IRIS_TASK_MESSAGE_ADD_OBSERVER + 10,
	IRIS_PROCESS_MESSAGE_ADD_SOURCE,
	IRIS_PROCESS_MESSAGE_ADD_SINK,
	IRIS_PROCESS_MESSAGE_CHAIN_CANCEL,
	IRIS_PROCESS_MESSAGE_ADD_WATCH,
	IRIS_PROCESS_MESSAGE_CHAIN_ESTIMATE
} IrisProcessMessageType;

struct _IrisProcessPrivate
{
	/* Structures for delivery and storage of work items. */
	IrisPort     *work_port;
	IrisReceiver *work_receiver;
	IrisQueue    *work_queue;

	/* Connections. These will be set to NULL if we get a DEP_CANCELED or
	 * DEP_FINISHED message from them (because we release our reference on them
	 * at that point)
	 */
	IrisProcess  *source, *sink;

	/* These are updated atomically from multiple threads. */
	volatile gint processed_items,
	              total_items,
	              estimated_total_items;

	/* Atomically accessed as a pointer ... */
	volatile gfloat *output_estimate_factor;

	/* These two are accessed from control message handler only */
	gint          watch_total_items;      /* the last value of total_items sent
	                                         to watchers */


	/* For status indicators. */
	volatile gchar *title;

	/* Monitoring UI */
	GList          *watch_port_list;      /* list of watchers */
	GTimer         *watch_timer;          /* timeouts to throttle status messages */
};

#endif /* __IRIS_PROCESS_PRIVATE_H__ */
