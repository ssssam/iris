/* iris-task-private.h
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

#ifndef __IRIS_TASK_PRIVATE_H__
#define __IRIS_TASK_PRIVATE_H__

#include <glib-object.h>

#include "iris-task.h"
#include "iris-port.h"
#include "iris-receiver.h"

#define IRIS_TASK_GET_PRIVATE(object)		\
	(G_TYPE_INSTANCE_GET_PRIVATE((object),	\
	 IRIS_TYPE_TASK, IrisTaskPrivate))

typedef enum
{
	IRIS_TASK_FLAG_EXECUTING = 1 << 0,
	IRIS_TASK_FLAG_CANCELED  = 1 << 1,
	IRIS_TASK_FLAG_FINISHED  = 1 << 2,
	IRIS_TASK_FLAG_ASYNC     = 1 << 3,
} IrisTaskFlags;

typedef enum
{
	IRIS_TASK_MESSAGE_EXECUTE,
	IRIS_TASK_MESSAGE_CANCEL,
	IRIS_TASK_MESSAGE_ADD_CALLBACK,
	IRIS_TASK_MESSAGE_ADD_ERRBACK,
	IRIS_TASK_MESSAGE_ADD_DEPENDENCY,
	IRIS_TASK_MESSAGE_REMOVE_DEPENDENCY,
	IRIS_TASK_MESSAGE_COMPLETE,
	IRIS_TASK_MESSAGE_CONTEXT,
	IRIS_TASK_MESSAGE_ERROR,
	IRIS_TASK_MESSAGE_RESULT,
} IrisTaskMessageType;

struct _IrisTaskPrivate
{
	IrisPort     *port;
	IrisReceiver *receiver;

	GValue        result;
	GError       *error;

	GClosure     *closure;
	GList        *handlers;

	glong         flags;
	GMainContext *context;
};

#endif /* __IRIS_TASK_PRIVATE_H__ */
