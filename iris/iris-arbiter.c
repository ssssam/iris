/* iris-arbiter.c
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

#include "iris-arbiter.h"
#include "iris-arbiter-private.h"
#include "iris-port.h"
#include "iris-receiver-private.h"

/**
 * SECTION:iris-arbiter
 * @title: IrisArbiter
 * @short_description: Arbitrate when and how messages can be received
 *
 * #IrisArbiter provides a way to control how messages can be
 * received.  The simple arbiter, created using iris_arbiter_receive()
 * does nothing to control when messages can be received.  Messages
 * will be processed as fast as the scheduler can handle them.
 *
 * Alternatively, the coordination-arbiter can be used with
 * iris_arbiter_coordinate().  The coordination-arbiter is similar to
 * a reader-writer lock implemented asynchronously.
 */

G_DEFINE_ABSTRACT_TYPE (IrisArbiter, iris_arbiter, G_TYPE_OBJECT)

GType
iris_receive_decision_get_type (void)
{
	static GType      gtype    = 0;
	static GEnumValue values[] = {
		{ IRIS_RECEIVE_NEVER, "IRIS_RECEIVE_NEVER", "NEVER" },
		{ IRIS_RECEIVE_NOW,   "IRIS_RECEIVE_NOW",   "NOW" },
		{ IRIS_RECEIVE_LATER, "IRIS_RECEIVE_LATER", "LATER" },
		{ 0, NULL, NULL }
	};

	if (G_UNLIKELY (!gtype))
		gtype = g_enum_register_static ("IrisReceiveDecision", values);

	return gtype;
}

static void
iris_arbiter_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_arbiter_parent_class)->finalize (object);
}

static void
iris_arbiter_class_init (IrisArbiterClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = iris_arbiter_finalize;
}

static void
iris_arbiter_init (IrisArbiter *arbiter)
{
	arbiter->priv = NULL;
}

/**
 * iris_arbiter_can_receive:
 * @arbiter: An #IrisArbiter
 * @receiver: An #IrisReceiver
 *
 * Checks to see if a receiver is allowed to receive.
 *
 * Return value: An #IrisReceiveDecision.
 */
IrisReceiveDecision
iris_arbiter_can_receive (IrisArbiter  *arbiter,
                          IrisReceiver *receiver)
{
	g_return_val_if_fail (IRIS_IS_ARBITER (arbiter), IRIS_RECEIVE_NEVER);
	if (IRIS_ARBITER_GET_CLASS (arbiter)->can_receive)
		return IRIS_ARBITER_GET_CLASS (arbiter)->can_receive (arbiter, receiver);
	return IRIS_RECEIVE_NOW;
}

/**
 * iris_arbiter_receive_completed:
 * @arbiter: An #IrisArbiter
 * @receiver: An #IrisReceiver
 *
 * Notifies @arbiter that a receive has been completed on @receiver.
 */
void
iris_arbiter_receive_completed (IrisArbiter  *arbiter,
                                IrisReceiver *receiver)
{
	IRIS_ARBITER_GET_CLASS (arbiter)->receive_completed (arbiter, receiver);
}

/**
 * iris_arbiter_receive:
 * @scheduler: An #IrisScheduler or %NULL
 * @port: An #IrisPort
 * @handler: An #IrisMessageHandler to execute when a message is received
 * @user_data: data for @callback
 * @destroy_notify: A #GDestroyNotify or %NULL
 *
 * Creates a new #IrisReceiver instance that executes @handler when a message
 * is received on the receiver.  Note that if you attach this to an arbiter,
 * a message posted to @port may not result in @callback being executed right
 * away.
 *
 * If not %NULL, @destroy_notify will be called when the receiver is destroyed.
 *
 * Return value: the newly created #IrisReceiver instance
 */
IrisReceiver*
iris_arbiter_receive (IrisScheduler      *scheduler,
                      IrisPort           *port,
                      IrisMessageHandler  handler,
                      gpointer            user_data,
                      GDestroyNotify      destroy_notify)
{
	IrisReceiver *receiver;

	if (!scheduler)
		scheduler = iris_get_default_control_scheduler ();

	receiver = g_object_new (IRIS_TYPE_RECEIVER, NULL);
	receiver->priv->callback = handler;
	receiver->priv->data = user_data;
	receiver->priv->notify = destroy_notify;
	receiver->priv->scheduler = g_object_ref (scheduler);
	receiver->priv->port = g_object_ref (port);
	iris_port_set_receiver (port, receiver);

	return receiver;
}
