/* iris-receiver-private.h
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

#ifndef __IRIS_RECEIVER_PRIVATE_H__
#define __IRIS_RECEIVER_PRIVATE_H__

#include "iris-arbiter.h"
#include "iris-receiver.h"
#include "iris-types.h"

G_BEGIN_DECLS

struct _IrisReceiverPrivate
{
	IrisScheduler *scheduler;  /* The scheduler we will dispatch our
	                            * work items to.  If NULL, we will
	                            * send to the default dispatcher.
	                            * We should really set the value to
	                            * the default when initializing to
	                            * avoid the costs of lookups and locks.
	                            */

	IrisArbiter   *arbiter;    /* The arbiter tells us if we can accept
	                            * an incoming message.
	                            */

	GMutex        *mutex;      /* Used to synchronous our requests to the
	                            * the arbiter.
	                            */

	IrisMessageHandler
	               callback;   /* The callback we should invoke inside of
	                            * the scheduler worker.
	                            */

	gpointer       data;       /* The data associated with the worker
	                            * callback for the method.
	                            */

	gboolean       persistent; /* If the receiver is persistent, meaning
	                            * we can accept more than one message.
	                            * Non-persistent receivers are to be
	                            * removed by a port after a message is
	                            * accepted for execution.
	                            */

	gboolean       completed;  /* If we are a non-persistent receiver and
	                            * have already received an item, we can
	                            * be marked as completed so that we never
	                            * accept another item.
	                            */

	IrisMessage   *message;    /* A message that we are holding onto
	                            * until an arbiter says we can execute.
	                            */

	volatile gint  active;     /* The current number of processing
	                            * messages.
	                            */
	
	gint           max_active; /* The maximum number of receives that
	                            * we can process concurrently.
	                            */
};


gboolean iris_receiver_has_scheduler (IrisReceiver *receiver);
gboolean iris_receiver_has_arbiter   (IrisReceiver *receiver);

G_END_DECLS

#endif /* __IRIS_RECEIVER_PRIVATE_H__ */
