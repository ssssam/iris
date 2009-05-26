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

struct _IrisArbiterPrivate
{
	gpointer dummy;
};

G_DEFINE_ABSTRACT_TYPE (IrisArbiter, iris_arbiter, G_TYPE_OBJECT);

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
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = iris_arbiter_finalize;

	g_type_class_add_private (object_class, sizeof (IrisArbiterPrivate));
}

static void
iris_arbiter_init (IrisArbiter *arbiter)
{
	arbiter->priv = G_TYPE_INSTANCE_GET_PRIVATE (arbiter,
	                                          IRIS_TYPE_ARBITER,
	                                          IrisArbiterPrivate);
}

IrisReceiveDecision
iris_arbiter_can_receive (IrisArbiter  *arbiter,
                          IrisReceiver *receiver)
{
	g_return_val_if_fail (IRIS_IS_ARBITER (arbiter), IRIS_RECEIVE_NEVER);
	if (IRIS_ARBITER_GET_CLASS (arbiter)->can_receive)
		return IRIS_ARBITER_GET_CLASS (arbiter)->can_receive (arbiter, receiver);
	return IRIS_RECEIVE_NOW;
}

void
iris_arbiter_receive_completed (IrisArbiter  *arbiter,
                                IrisReceiver *receiver)
{
	IRIS_ARBITER_GET_CLASS (arbiter)->receive_completed (arbiter, receiver);
}

/**
 * iris_receiver_receive:
 * @scheduler: An #IrisScheduler or %NULL
 * @port: An #IrisPort
 * @callback: An #IrisMessageHandler to execute when a message is received
 * @user_data: data for @callback
 * @notify: A #GDestroyNotify or %NULL
 *
 * Creates a new #IrisReceiver instance that executes @callback when a message
 * is received on the receiver.  Note that if you attach this to an arbiter,
 * a message posted to @port may not result in @callback being executed right
 * away.
 *
 * If not %NULL, @notify will be called when the receiver is destroyed.
 *
 * Return value: the newly created #IrisReceiver instance
 */
IrisReceiver*
iris_arbiter_receive (IrisScheduler      *scheduler,
                      IrisPort           *port,
                      IrisMessageHandler  callback,
                      gpointer            user_data,
                      GDestroyNotify      notify)
{
	IrisReceiver *receiver;

	if (!scheduler)
		scheduler = iris_scheduler_default ();

	receiver = g_object_new (IRIS_TYPE_RECEIVER, NULL);
	receiver->priv->callback = callback;
	receiver->priv->data = user_data;
	receiver->priv->notify = notify;
	receiver->priv->scheduler = g_object_ref (scheduler);
	receiver->priv->port = g_object_ref (port);
	iris_port_set_receiver (port, receiver);

	return receiver;
}

IrisArbiter*
iris_arbiter_coordinate (IrisReceiver *exclusive,
                         IrisReceiver *concurrent,
                         IrisReceiver *teardown)
{
	return iris_coordination_arbiter_new (exclusive, concurrent, teardown);
}
