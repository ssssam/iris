/* iris-task.c
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

#include "iris-task.h"
#include "iris-port.h"
#include "iris-receiver.h"

typedef enum
{
	IRIS_TASK_READY,
	IRIS_TASK_WAITING,
	IRIS_TASK_EXECUTING,
	IRIS_TASK_CALLBACKS,
	IRIS_TASK_CANCELED,
	IRIS_TASK_FINISHED
} IrisTaskState;

struct _IrisTaskPrivate
{
	IrisTaskState state;

	IrisPort *port;  /* message port for controlling state for the
	                  * task. this is used with an exclusive arbiter
	                  * so that state changes happen synchronously.
	                  */
};

G_DEFINE_TYPE (IrisTask, iris_task, G_TYPE_OBJECT)

static void
iris_task_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_task_parent_class)->finalize (object);
}

static void
iris_task_class_init (IrisTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = iris_task_finalize;
	g_type_class_add_private (object_class, sizeof (IrisTaskPrivate));
}

static void
iris_task_init (IrisTask *task)
{
	task->priv = G_TYPE_INSTANCE_GET_PRIVATE (task,
	                                          IRIS_TYPE_TASK,
	                                          IrisTaskPrivate);
}

IrisTask*
iris_task_new (void)
{
	return g_object_new (IRIS_TYPE_TASK, NULL);
}
