/* iris-receiver.c
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

#include "iris-receiver.h"
#include "iris-receiver-private.h"

struct _IrisReceiverPrivate
{
	IrisScheduler *scheduler; /* The scheduler we will dispatch our
	                           * work items to.  If NULL, we will
	                           * send to the default dispatcher.
	                           * We should really set the value to
	                           * the default when initializing to
	                           * avoid the costs of lookups and locks.
	                           */

	IrisArbiter   *arbiter;   /* The arbiter tells us if we can accept
	                           * an incoming message.
	                           */
};

G_DEFINE_TYPE (IrisReceiver, iris_receiver, G_TYPE_OBJECT);

static IrisDeliveryStatus
_iris_receiver_deliver_real (IrisReceiver *receiver,
                             IrisMessage  *message)
{
	g_return_val_if_fail (message != NULL, IRIS_DELIVERY_REMOVE);
	return IRIS_DELIVERY_ACCEPTED;
}

static void
iris_receiver_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_receiver_parent_class)->finalize (object);
}

static void
iris_receiver_class_init (IrisReceiverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->deliver = _iris_receiver_deliver_real;
	object_class->finalize = iris_receiver_finalize;

	g_type_class_add_private (object_class, sizeof (IrisReceiverPrivate));
}

static void
iris_receiver_init (IrisReceiver *receiver)
{
	receiver->priv = G_TYPE_INSTANCE_GET_PRIVATE (receiver,
	                                              IRIS_TYPE_RECEIVER,
	                                              IrisReceiverPrivate);
}

/**
 * iris_receiver_new:
 *
 * Creates a new instance of #IrisReceiver.  Receivers are used to
 * translate incoming messages on ports into actionable work items
 * to be pushed onto the scheduler.
 *
 * Return value: The newly created #IrisReceiver.
 */
IrisReceiver*
iris_receiver_new (void)
{
	return g_object_new (IRIS_TYPE_RECEIVER, NULL);
}

/**
 * iris_receiver_deliver:
 * @receiver: An #IrisReceiver
 * @message: An #IrisMessage
 *
 * Delivers a message to the receiver so that the receiver may take an
 * action on the message.
 */
IrisDeliveryStatus
iris_receiver_deliver (IrisReceiver *receiver,
                       IrisMessage  *message)
{
	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), IRIS_DELIVERY_REMOVE);
	return IRIS_RECEIVER_GET_CLASS (receiver)->deliver (receiver, message);
}

/**
 * iris_receiver_new_full:
 * @scheduler: An #IrisScheduler
 * @arbiter: An #IrisArbiter
 *
 * Creates a new instance of #IrisReceiver.  The receiver is initialized
 * to dispatch work items to @scheduler.  Messages can only be dispatched
 * if the arbiter allows us to.
 *
 * Return value: The newly created #IrisReceiver instance.
 */
IrisReceiver*
iris_receiver_new_full (IrisScheduler *scheduler,
                        IrisArbiter   *arbiter)
{
	IrisReceiver        *receiver;
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_SCHEDULER (scheduler), NULL);
	g_return_val_if_fail (IRIS_IS_ARBITER (arbiter), NULL);

	receiver = iris_receiver_new ();
	priv = receiver->priv;

	priv->scheduler = scheduler;
	priv->arbiter = arbiter;

	return receiver;
}

/**
 * iris_receiver_has_arbiter:
 * @receiver: An #IrisReceiver
 *
 * Private, used by unit tests and internally only.
 *
 * Determines if the receiver currently has an arbiter attached.
 *
 * Return value: TRUE if an arbiter exists.
 */
gboolean
iris_receiver_has_arbiter (IrisReceiver *receiver)
{
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), FALSE);

	priv = receiver->priv;

	return g_atomic_pointer_get (&priv->arbiter) != NULL;
}

/**
 * iris_receiver_has_scheduler:
 * @receiver: An #IrisReceiver
 *
 * Private, used by unit tests and internally only.
 *
 * Determines if the receiver currently has a scheduler attached.
 *
 * Return value: TRUE if a scheduler exists.
 */
gboolean
iris_receiver_has_scheduler (IrisReceiver *receiver)
{
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), FALSE);

	priv = receiver->priv;

	return g_atomic_pointer_get (&priv->scheduler) != NULL;
}
