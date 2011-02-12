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

#include "iris-debug.h"
#include "iris-port.h"
#include "iris-port-private.h"
#include "iris-receiver.h"
#include "iris-receiver-private.h"

/**
 * SECTION:iris-port
 * @title: IrisPort
 * @short_description: Light-weight message delivery
 *
 * #IrisPort is a structure used for delivering messages.  When a port is
 * connected to a receiver they can be used to perform actions when a message
 * is delivered.  See iris_arbiter_receive() for more information.
 */

#define IRIS_PORT_STATE_PAUSED 1 << 0
#define PORT_PAUSED(port)                                         \
	((g_atomic_int_get (&port->priv->state)                   \
	  & IRIS_PORT_STATE_PAUSED) != 0)
#define STORE_MESSAGE(p,m)                                        \
	G_STMT_START {                                            \
		if (!p->current)                                  \
			p->current = iris_message_ref (m);        \
		else {                                            \
			if (!p->queue)                            \
				p->queue = g_queue_new ();        \
			g_queue_push_tail (p->queue,              \
					iris_message_ref (m));    \
		}                                                 \
	} G_STMT_END

G_DEFINE_TYPE (IrisPort, iris_port, G_TYPE_OBJECT)

static void
iris_port_set_receiver_real (IrisPort     *port,
                             IrisReceiver *receiver)
{
	IrisPortPrivate *priv;
	gboolean         flush = FALSE;

	g_return_if_fail (IRIS_IS_PORT (port));
	g_return_if_fail (receiver == NULL || IRIS_IS_RECEIVER (receiver));

	priv = port->priv;

	g_mutex_lock (priv->mutex);

	/* priv->receiver is immutable after the first message is sent and not
	 * accessed before. */
	if (receiver != priv->receiver) {
		if (priv->receiver) {
			/* FIXME: Unhook current receiver */
			g_object_unref ((gpointer)priv->receiver);
		}

		if (receiver) {
			priv->receiver = g_object_ref (receiver);
			flush = TRUE;
			/* FIXME: Hook current receiver? */
		}
		else {
			priv->receiver = NULL;
		}
	}

	g_mutex_unlock (priv->mutex);

	if (flush)
		iris_port_flush (port, NULL);
}

static void
iris_port_finalize (GObject *object)
{
	IrisPort        *port;
	IrisPortPrivate *priv;

	port = IRIS_PORT (port);
	priv = port->priv;

	if (priv->queue != NULL)
		g_queue_free (priv->queue);

	G_OBJECT_CLASS (iris_port_parent_class)->finalize (object);
}

static void
iris_port_class_init (IrisPortClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
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
	port->priv->state = 0;
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

static IrisDeliveryStatus
iris_port_pause (IrisPort     *port,
                 IrisReceiver *receiver,
                 IrisMessage  *message)
{
	IrisPortPrivate    *priv;
	IrisDeliveryStatus  delivered;

	priv = port->priv;

	g_mutex_lock (priv->mutex);

	/* Between getting IRIS_DELIVERY_PAUSE and actually receiving the mutex,
	 * all of the active messages could have completed. This would mean no more
	 * calls to iris_receiver_resume(), so if we just queue the message anyway
	 * the port will stay paused forever. We must recheck the delivery state
	 * within the mutex, where no more messages can complete because they will
	 * block in iris_port_flush().
	 */
	delivered = iris_receiver_deliver (receiver, message);

	switch (delivered) {
		case IRIS_DELIVERY_ACCEPTED: break;
		case IRIS_DELIVERY_ACCEPTED_PAUSE:
			priv->state |= IRIS_PORT_STATE_PAUSED;
			break;
		case IRIS_DELIVERY_PAUSE:
			/* Further attempts to stop the port freezing up */
			g_warn_if_fail (g_atomic_int_get (&priv->receiver->priv->active) > 0);

			priv->state |= IRIS_PORT_STATE_PAUSED;
			STORE_MESSAGE (priv, message);
			break;
		case IRIS_DELIVERY_REMOVE:
			STORE_MESSAGE (priv, message);
			g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
			break;
		case IRIS_DELIVERY_ACCEPTED_REMOVE:
			g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
			break;
		default:
			g_warn_if_reached ();
	}

	g_mutex_unlock (priv->mutex);

	return delivered;
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
	IrisReceiver       *receiver;
	IrisDeliveryStatus  delivered;

	iris_debug (IRIS_DEBUG_PORT);

	g_return_if_fail (IRIS_IS_PORT (port));
	g_return_if_fail (message != NULL);

	priv = port->priv;
	receiver = g_atomic_pointer_get (&priv->receiver);

	if (PORT_PAUSED (port) || !receiver) {
		g_mutex_lock (priv->mutex);

		if (receiver == NULL) {
			STORE_MESSAGE (priv, message);
			g_mutex_unlock (priv->mutex);
			return;
		}

		if (priv->current != NULL) {
			if (priv->queue == NULL)
				priv->queue = g_queue_new ();

			g_queue_push_tail (priv->queue, iris_message_ref (message));
			g_mutex_unlock (priv->mutex);
			return;
		}

		/* If we are paused but nothing is queued, let's unpause and try
		 * delivering the message. This fixes a problem with synchronous
		 * schedulers where a message triggers another one, but because this
		 * all happens inside the receiver worker function the receiver hasn't
		 * had a chance to unpause our queue yet.
		 */
		g_mutex_unlock (priv->mutex);
	}

	/* No locking here, so that the case of a receiver with no arbiter executes
	 * very fast.
	 */
	delivered = iris_receiver_deliver (receiver, message);

	switch (delivered) {
		case IRIS_DELIVERY_ACCEPTED:
			break;
		case IRIS_DELIVERY_ACCEPTED_PAUSE:
			g_mutex_lock (priv->mutex);
			priv->state |= IRIS_PORT_STATE_PAUSED;
			g_mutex_unlock (priv->mutex);
			break;
		case IRIS_DELIVERY_PAUSE:
			iris_port_pause (port, receiver, message);
			break;
		case IRIS_DELIVERY_REMOVE:
			/* store message and fall-through */
			g_mutex_lock (priv->mutex);
			STORE_MESSAGE (priv, message);
			g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
			g_mutex_unlock (priv->mutex);
			break;
		case IRIS_DELIVERY_ACCEPTED_REMOVE:
			g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
			break;
		default:
			g_warn_if_reached ();
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

/**
 * iris_port_flush:
 * @port: An #IrisPort
 * @repost_message: An #IrisMessage, or %NULL.
 *
 * Flushes the port by trying to redeliver messages to a listening
 * #IrisReceiver. @repost_message will be delivered before the port's queue.
 */
/* FIXME: would this function be better called iris_port_resume() ? There's never a reason
 * to actually flush the port, unless you are closing it ... */
void
iris_port_flush (IrisPort    *port,
                 IrisMessage *repost_message)
{
	IrisPortPrivate    *priv;
	IrisMessage        *message;
	IrisReceiver       *receiver;
	IrisDeliveryStatus  delivered;

	iris_debug (IRIS_DEBUG_PORT);

	g_return_if_fail (IRIS_IS_PORT (port));

	priv = port->priv;
	receiver = g_atomic_pointer_get (&priv->receiver);

	if (receiver == NULL)
		return;

	g_mutex_lock (priv->mutex);

	/* Port should probably be paused anyway. We leave it set, to force
	 * iris_port_post() to queue all items while we are working.
	 */
	priv->state &= IRIS_PORT_STATE_PAUSED;

	do {
		if (repost_message != NULL) {
			iris_message_ref (repost_message);
			message = repost_message;
			repost_message = NULL;
		} else
		if (priv->queue != NULL && g_queue_get_length (priv->queue) > 0) {
			/* 'current' is effectively the head of the queue */
			g_warn_if_fail (priv->current != NULL);
			message = priv->current;
			priv->current = g_queue_pop_head (priv->queue);
		} else {
			message = priv->current;
			priv->current = NULL;
		}

		if (message == NULL) {
			/* Unpause now the queue is empty */
			priv->state &= ~IRIS_PORT_STATE_PAUSED;
			g_mutex_unlock (priv->mutex);
			break;
		}
		g_mutex_unlock (priv->mutex);

		delivered = iris_receiver_deliver (receiver, message);

		switch (delivered) {
			case IRIS_DELIVERY_ACCEPTED: break;
			case IRIS_DELIVERY_ACCEPTED_PAUSE: break;
			case IRIS_DELIVERY_PAUSE:
				delivered = iris_port_pause (port, receiver, message);
				break;
			case IRIS_DELIVERY_REMOVE:
				g_mutex_lock (priv->mutex);
				STORE_MESSAGE (priv, message);
				g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
				g_mutex_unlock (priv->mutex);
				break;
			case IRIS_DELIVERY_ACCEPTED_REMOVE:
				g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
				break;
			default: g_warn_if_reached ();
		}

		iris_message_unref (message);

		if (delivered != IRIS_DELIVERY_ACCEPTED && delivered != IRIS_DELIVERY_ACCEPTED_PAUSE)
			break;

		g_mutex_lock (priv->mutex);
	} while (1);

	/* Don't free the queue even if it's empty. We will probably need it again. */
}

/**
 * iris_port_is_paused:
 * @port: An #IrisPort
 *
 * Checks if the port is currently paused.
 *
 * Return value: %TRUE if the port is paused.
 */
gboolean
iris_port_is_paused (IrisPort *port)
{
	return PORT_PAUSED (port);
}
