/* iris-port.c
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

#include "iris-port.h"

#define IRIS_PORT_STATE_PAUSED 1 << 0

#define PORT_PAUSED(port)                         \
	((g_atomic_int_get (&port->priv->state)   \
	  & IRIS_PORT_STATE_PAUSED) != 0)

struct _IrisPortPrivate
{
	GQueue       *queue;
	IrisReceiver *receiver;
	GMutex       *mutex;
	gint          state;
	IrisMessage  *current;
};

G_DEFINE_TYPE (IrisPort, iris_port, G_TYPE_OBJECT);

static void
iris_port_set_receiver_real (IrisPort     *port,
                             IrisReceiver *receiver)
{
	IrisPortPrivate *priv;

	g_return_if_fail (IRIS_IS_PORT (port));
	g_return_if_fail (receiver == NULL || IRIS_IS_RECEIVER (receiver));

	priv = port->priv;

	g_mutex_lock (priv->mutex);

	if (!priv->receiver) {
		// FIXME: Unhook current receiver
	}

	if (receiver) {
		priv->receiver = g_object_ref (receiver);
		// FIXME: Hook current receiver
	}
	else {
		priv->receiver = NULL;
	}

	g_mutex_unlock (priv->mutex);
}

static void
iris_port_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_port_parent_class)->finalize (object);
}

static void
iris_port_class_init (IrisPortClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->set_receiver = iris_port_set_receiver_real;
	object_class->finalize = iris_port_finalize;

	g_type_class_add_private (object_class, sizeof (IrisPortPrivate));
}

static void
iris_port_init (IrisPort *port)
{
	port->priv = G_TYPE_INSTANCE_GET_PRIVATE (port,
	                                          IRIS_TYPE_PORT,
	                                          IrisPortPrivate);

	port->priv->mutex = g_mutex_new ();
}

/**
 * iris_port_new:
 *
 * Creates a new instance of an #IrisPort.
 *
 * Return value: The newly created #IrisPort
 */
IrisPort*
iris_port_new (void)
{
	return g_object_new (IRIS_TYPE_PORT, NULL);
}

/**
 * iris_port_post:
 * @port: An #IrisPort
 * @message: The #IrisMessage to post
 *
 * Posts @message to the port.  Any receivers listening to the port will
 * receive the message.
 */
void
iris_port_post (IrisPort    *port,
                IrisMessage *message)
{
	IrisPortPrivate    *priv;
	IrisReceiver       *receiver    = NULL;
	IrisDeliveryStatus  delivered;
	gint                state;

	g_return_if_fail (IRIS_IS_PORT (port));
	g_return_if_fail (message != NULL);

	priv = port->priv;

	if (PORT_PAUSED (port) || !priv->receiver) {
		g_mutex_lock (priv->mutex);
		if (PORT_PAUSED (port) || !priv->receiver) {
			if (!priv->queue)
				priv->queue = g_queue_new ();
			g_queue_push_head (priv->queue, message);
		}
		else {
			receiver = priv->receiver;
			state = priv->state;
		}
		g_mutex_unlock (priv->mutex);
	}
	else {
		receiver = priv->receiver;
	}

	if (receiver) {
		delivered = iris_receiver_deliver (receiver, message);

		switch (delivered) {
		case IRIS_DELIVERY_ACCEPTED:
			break;
		case IRIS_DELIVERY_ACCEPTED_PAUSE:
			g_mutex_lock (priv->mutex);
			priv->state |= IRIS_PORT_STATE_PAUSED;
			g_mutex_unlock (priv->mutex);
			break;
		case IRIS_DELIVERY_REMOVE:
			/* store message and fall-through */
			priv->current = message;
		case IRIS_DELIVERY_ACCEPTED_REMOVE:
			g_mutex_lock (priv->mutex);
			if (priv->receiver == receiver)
				priv->receiver = NULL;
			g_mutex_unlock (priv->mutex);
			break;
		default:
			g_assert_not_reached ();
		}
	}
}

/**
 * iris_port_has_receiver:
 * @port: An #IrisPort
 *
 * Determines if the port is currently connected to a receiver.
 *
 * Return value: TRUE if there is a receiver hooked up.
 */
gboolean
iris_port_has_receiver (IrisPort *port)
{
	g_return_val_if_fail (IRIS_IS_PORT (port), FALSE);
	return (g_atomic_pointer_get (&port->priv->receiver) != NULL);
}

/**
 * iris_port_set_receiver:
 * @port: An #IrisPort
 * @receiver: An #IrisReceiver
 *
 * Sets the current receiver for the port.  If a receiver already
 * exists, it will be removed.
 */
void
iris_port_set_receiver (IrisPort     *port,
                        IrisReceiver *receiver)
{
	g_return_if_fail (IRIS_IS_PORT (port));

	if (IRIS_PORT_GET_CLASS (port)->set_receiver)
		IRIS_PORT_GET_CLASS (port)->set_receiver (port, receiver);
}

/**
 * iris_port_get_receiver:
 * @port: An #IrisPort
 *
 * Retreives the currently attached receiver for the port.
 *
 * Return value: An #IrisReceiver instance or %NULL.
 */
IrisReceiver*
iris_port_get_receiver (IrisPort *port)
{
	g_return_val_if_fail (IRIS_IS_PORT (port), NULL);
	return g_atomic_pointer_get (&port->priv->receiver);
}

/**
 * iris_port_get_queue_count:
 * @port: An #IrisPort
 *
 * Retreives the count of queued items still waiting to be delivered to
 * a receiver.
 *
 * Return value: a #gint of the number of queued messages.
 */
guint
iris_port_get_queue_count (IrisPort *port)
{
	IrisPortPrivate *priv;
	guint            queue_count;

	g_return_val_if_fail (IRIS_IS_PORT (port), 0);

	priv = port->priv;

	g_mutex_lock (priv->mutex);
	queue_count = priv->current != NULL ? 1 : 0;
	if (priv->queue)
		queue_count += g_queue_get_length (priv->queue);
	g_mutex_unlock (priv->mutex);

	return queue_count;
}
