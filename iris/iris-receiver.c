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

struct _IrisReceiverPrivate
{
};

G_DEFINE_TYPE (IrisReceiver, iris_receiver, G_TYPE_OBJECT);

static IrisDeliveryStatus
_iris_receiver_deliver_real (IrisReceiver *receiver,
                             IrisMessage  *message)
{
	/* NOTE: receiver is gauranteed to be valid since we call it from
	 *   iris_receiver_deliver(). Therefore, just save the time and
	 *   not check it.
	 */

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
