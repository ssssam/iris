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

#include "iris-gmainscheduler.h"
#include "iris-task.h"
#include "iris-port.h"
#include "iris-receiver.h"

#define IRIS_TASK_GET_PRIVATE(object)		\
	(G_TYPE_INSTANCE_GET_PRIVATE((object),	\
	 IRIS_TYPE_TASK, IrisTaskPrivate))

typedef enum
{
	/* Basic state */
	IRIS_TASK_FLAG_STARTED  = 1 << 0,  /* set when work function starts, task
	                                      is immutable once set */
	IRIS_TASK_FLAG_FINISHED = 1 << 1,  /* corresponds to
	                                      iris_task_is_finished() */

	/* More detailed state */
	IRIS_TASK_FLAG_NEED_EXECUTE       = 1 << 2,
	IRIS_TASK_FLAG_WORK_ACTIVE        = 1 << 3,
	IRIS_TASK_FLAG_CALLBACKS_ACTIVE   = 1 << 4,
	IRIS_TASK_FLAG_CANCELLED           = 1 << 5,

	/* A couple of other flags are currently volatile gint, but they can be
	 * moved here once we make all flag access atomic ... */

	IRIS_TASK_FLAG_ASYNC              = 1 << 6
} IrisTaskFlags;

typedef enum
{
	IRIS_TASK_MESSAGE_START_WORK = 1,
	IRIS_TASK_MESSAGE_WORK_FINISHED,
	IRIS_TASK_MESSAGE_PROGRESS_CALLBACKS,
	IRIS_TASK_MESSAGE_CALLBACKS_FINISHED,
	IRIS_TASK_MESSAGE_START_CANCEL,
	IRIS_TASK_MESSAGE_FINISH_CANCEL,
	IRIS_TASK_MESSAGE_FINISH,
	IRIS_TASK_MESSAGE_ADD_HANDLER,
	IRIS_TASK_MESSAGE_ADD_DEPENDENCY,
	IRIS_TASK_MESSAGE_REMOVE_DEPENDENCY,   /* 10 */
	IRIS_TASK_MESSAGE_SET_MAIN_CONTEXT,
	IRIS_TASK_MESSAGE_DEP_FINISHED,
	IRIS_TASK_MESSAGE_DEP_CANCELLED,
	IRIS_TASK_MESSAGE_ADD_OBSERVER,
	IRIS_TASK_MESSAGE_REMOVE_OBSERVER
} IrisTaskMessageType;

typedef struct
{
	GClosure *callback;
	GClosure *errback;
} IrisTaskHandler;

struct _IrisTaskPrivate
{
	IrisPort      *port;          /* Message delivery port */
	IrisReceiver  *receiver;      /* Receiver for port */

	IrisScheduler *control_scheduler,
	              *work_scheduler;

	IrisProgressMode progress_mode;

	GMutex        *mutex;         /* Mutex for result/error */
	GValue         result;        /* Current task result */
	GError        *error;         /* Current task error */
	GClosure      *closure;       /* Our execution closure. */
	GList         *handlers;      /* A list of callback/errback handlers.
	                               */
	GList         *dependencies;  /* Tasks we are depending on. */
	GList         *observers;     /* Tasks observing our state changes */

	volatile gint  flags;
	volatile gint  cancel_finished;  /* This can become a normal flag when
	                                    we get atomic flag setting */
	gint        in_message_handler;  /* Used to pass iris_receiver_destroy()
	                                    correct params and avoid a hang */

	GMainContext  *context;       /* A main-context to execute our
	                               * callbacks and async_result within.
	                               */
	IrisGMainScheduler
	              *context_sched; /* An IrisGMainScheduler used to perform
	                               * work items within a main-contenxt.
	                               */
	GAsyncResult  *async_result;  /* GAsyncResult to execute after our
	                               * execution/callbacks have completed.
	                               */
};

/* Internals exposed for IrisProcess etc. */
void iris_task_remove_dependency_sync (IrisTask *task, IrisTask *dep);
void iris_task_progress_callbacks (IrisTask *task);
void iris_task_notify_observers (IrisTask *task);

#endif /* __IRIS_TASK_PRIVATE_H__ */
