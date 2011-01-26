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
	/* Last IRIS_TASK_FLAG is 1 << 5, let's leave some space. */
	IRIS_PROCESS_FLAG_NO_MORE_WORK    = 1 << 10,
	IRIS_PROCESS_FLAG_HAS_SOURCE      = 1 << 11,
	IRIS_PROCESS_FLAG_HAS_SINK        = 1 << 12
} IrisProcessFlags;

typedef enum
{
	IRIS_PROCESS_MESSAGE_NO_MORE_WORK = IRIS_TASK_MESSAGE_ADD_OBSERVER + 10,
	IRIS_PROCESS_MESSAGE_ADD_SOURCE,
	IRIS_PROCESS_MESSAGE_ADD_SINK,
	IRIS_PROCESS_MESSAGE_ADD_WATCH
} IrisProcessMessageType;

struct _IrisProcessPrivate
{
	/* Structures for delivery and storage of work items. */
	IrisPort     *work_port;
	IrisReceiver *work_receiver;
	IrisQueue    *work_queue;

	/* Connections */
	IrisProcess  *source, *sink;

	gint processed_items,    /* updated atomically from the worker thread as
	                            work items are completed */
	     total_items,        /* updated atomically by iris_process_enqueue() */
	     total_items_pushed; /* tracks the last value of total_items sent to
	                            watchers */

	/* For status indicators. */
	gchar        *title;

	/* Monitoring UI */
	GList        *watch_port_list;  /* List of watchers */
	GTimer       *watch_timer;      /* Timeouts to throttle status messages */
};

#endif /* __IRIS_PROCESS_PRIVATE_H__ */
