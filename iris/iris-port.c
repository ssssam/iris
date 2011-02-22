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
 *
 * The order in which the messages were posted is guaranteed to be preserved,
 * but by default #IrisReceiver may call the message handler simultaneously in
 * different threads. If the order is important, use iris_arbiter_coordinate()
 * to make the receiver <firstterm>exclusive</firstterm>, which guarantees that
 * the messages will be processed one at a time.
 */

#define PORT_IS_PAUSED(p)   (g_atomic_int_get (&p->priv->paused))
#define PORT_IS_FLUSHING(p) (g_atomic_int_get (&p->priv->flushing))

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
		iris_port_flush (port);
}

static void
iris_port_finalize (GObject *object)
{
	IrisPort        *port;
	IrisPortPrivate *priv;

	port = IRIS_PORT (object);
	priv = port->priv;

	if (priv->queue != NULL)
		g_queue_free (priv->queue);

	g_mutex_free (priv->mutex);

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

	port->priv->paused = FALSE;
	port->priv->flushing = FALSE;
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

static void
store_message_at_head_ul (IrisPort    *port, 
                          IrisMessage *message)
{
	IrisPortPrivate *priv = port->priv;

	if (priv->current) {
		if (!priv->queue)
			priv->queue = g_queue_new ();

		g_queue_push_head (priv->queue, priv->current);
	}

	priv->current = iris_message_ref_sink (message);
}

static void
store_message_at_tail_ul (IrisPort    *port, 
                          IrisMessage *message) {
	IrisPortPrivate *priv = port->priv;

	if (!priv->current) {
		g_warn_if_fail (!priv->queue || g_queue_get_length (priv->queue)==0);

		priv->current = iris_message_ref_sink (message);
	} else {
		if (!priv->queue)
			priv->queue = g_queue_new();

		g_queue_push_tail (priv->queue, iris_message_ref (message));
	}
}

/* Default way to post a message, inside the port lock so no races can occur
 * with iris_port_flush(). (These are dangerous because when a receiver's
 * last message completes the port must be flushed so it doesn't freeze up).
 */
static IrisDeliveryStatus
post_with_lock_ul (IrisPort     *port,
                   IrisReceiver *receiver,
                   IrisMessage  *message,
                   gboolean      queue_at_head)
{
	IrisPortPrivate    *priv;
	IrisDeliveryStatus  delivered;

	priv = port->priv;

	delivered = iris_receiver_deliver (receiver, message);

	switch (delivered) {
		case IRIS_DELIVERY_ACCEPTED:
			break;
		case IRIS_DELIVERY_PAUSE:
			g_atomic_int_set (&priv->paused, TRUE);
			queue_at_head ? store_message_at_head_ul (port, message):
			                store_message_at_tail_ul (port, message);
			break;
		case IRIS_DELIVERY_REMOVE:
			queue_at_head ? store_message_at_head_ul (port, message):
			                store_message_at_tail_ul (port, message);
			g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
			break;
		case IRIS_DELIVERY_ACCEPTED_REMOVE:
			g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);
			break;
		default:
			g_warn_if_reached ();
	}

	return delivered;
}


/**
 * iris_port_post:
 * @port: An #IrisPort
 * @message: The #IrisMessage to post
 *
 * Posts @message to the port.  Any receivers listening to the port will
 * receive the message.
 *
 * Once an #IrisMessage is posted, the port will either sink its floating
 * reference (if it is still floating) or add a new reference. The message will
 * then be kept alive until the message is delivered. This means the following
 * is all you need to do post a message:
 * |[
 *   iris_port_post (iris_message_new (MY_MESSAGE));
 * ]|
 *
 * Be aware that to post one message to multiple ports, things get slightly less
 * easy. It's possible that you might post the new message to the first port,
 * which then delivers and frees the message before you have had the chance to
 * deliver it to the second. To avoid this race condition, add an extra
 * reference before you post:
 * <example>
 * <title>Posting a message to multiple ports</title>
 * <programlisting>
 *   message = iris_message_new (56);
 *
 *   iris_message_ref (message);
 *
 *   for (node=port_list; node; node=node->next)
 *       iris_port_post (IRIS_PORT (node->data), message);
 *
 *   /&ast; Now each port has a reference and we can remove ours &ast;/
 *   iris_message_unref (message);
 * </programlisting></example>
 * The order that the messages are posted in will be preserved, but the
 * scheduler may call the #IrisReceiver<!-- -->'s message handler from multiple
 * threads. To avoid this, use iris_arbiter_coordinate() to make the receiver
 * <firstterm>exclusive</firstterm>.
 */
void
iris_port_post (IrisPort    *port,
                IrisMessage *message)
{
	IrisPortPrivate    *priv;
	IrisReceiver       *receiver;
	IrisDeliveryStatus  delivered;
	gboolean            was_paused;

	iris_debug (IRIS_DEBUG_PORT);

	g_return_if_fail (IRIS_IS_PORT (port));
	g_return_if_fail (message != NULL);

	priv = port->priv;
	receiver = g_atomic_pointer_get (&priv->receiver);

	if (PORT_IS_PAUSED (port) || !receiver) {
		g_mutex_lock (priv->mutex);

		if (!PORT_IS_PAUSED (port) && receiver) {
			/* Port has reopened since we acquired the mutex. This means that we
			 * were waiting on a flush or post which has now completed. We must
			 * deliver before releasing the mutex to preserve message ordering.
			 */
			post_with_lock_ul (port, receiver, message, FALSE);
		}
		else if (receiver == NULL) {
			store_message_at_tail_ul (port, message);
		}
		else if (priv->current == NULL && !PORT_IS_FLUSHING (port)) {
			/* Avoid freezing in synchronous schedulers. The port should be
			 * unpaused when the receiver's last message completes, but if
			 * the message has triggered another and the scheduler executes
			 * it straight away we have no other way to unpause than this.
			 */
			g_warn_if_fail (priv->queue == NULL || g_queue_get_length (priv->queue) == 0);

			was_paused = g_atomic_int_compare_and_exchange (&priv->paused, TRUE, FALSE);
			g_warn_if_fail (was_paused);

			post_with_lock_ul (port, receiver, message, FALSE);
		}
		else
			store_message_at_tail_ul (port, message);

		g_mutex_unlock (priv->mutex);
		return;
	}

	/* Lock-free post, so the case of a receiver with no arbiter runs fast.
	 * This is dangerous, because the receiver's state can change between
	 * us receiving IRIS_DELIVERY_PAUSE and actually queuing the message. For
	 * this reason, on pause we redeliver with the port locked to avoid the
	 * races that would make the port freeze up.
	 */
	delivered = iris_receiver_deliver (receiver, message);

	switch (delivered) {
		case IRIS_DELIVERY_ACCEPTED:
			break;
		case IRIS_DELIVERY_PAUSE:
			g_mutex_lock (priv->mutex);
			post_with_lock_ul (port, receiver, message, FALSE);
			g_mutex_unlock (priv->mutex);
			break;
		case IRIS_DELIVERY_REMOVE:
			/* store message and fall-through */
			g_mutex_lock (priv->mutex);
			store_message_at_tail_ul (port, message);
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
 * Return value: %TRUE if there is a receiver hooked up.
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
 * iris_port_get_queue_length:
 * @port: An #IrisPort
 *
 * Retreives the count of queued items still waiting to be delivered to
 * a receiver.
 *
 * Return value: a #gint of the number of queued messages.
 */
guint
iris_port_get_queue_length (IrisPort *port)
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
 *
 * Flushes the port by trying to redeliver messages to a listening
 * #IrisReceiver.
 */
/* FIXME: would this function be better called iris_port_resume() ? There's never a reason
 * to actually flush the port, unless you are closing it ... */
void
iris_port_flush (IrisPort    *port)
{
	IrisPortPrivate    *priv;
	IrisMessage        *message;
	IrisReceiver       *receiver;
	IrisDeliveryStatus  delivered;
	gboolean            flag_was_set;

	iris_debug (IRIS_DEBUG_PORT);

	g_return_if_fail (IRIS_IS_PORT (port));

	priv = port->priv;
	receiver = g_atomic_pointer_get (&priv->receiver);

	if (receiver == NULL)
		return;

	g_mutex_lock (priv->mutex);

	if (g_atomic_int_get (&priv->flushing)) {
		/* No need to run the flush if we got here. It is vital that after an
		 * exclusive receiver's last message is handled the port is flushed so
		 * it can unpause. The existing one only unlocks the mutex in one place
		 * and if it is told to pause, will try to deliver again inside the
		 * mutex. That code path with act exactly like the flush called from
		 * the receiver's last message.
		 */
		g_mutex_unlock (priv->mutex);
		return;
	}
	g_atomic_int_set (&priv->paused, TRUE);
	g_atomic_int_set (&priv->flushing, TRUE);

	do {
		/* Get next message; remember 'current' is effectively the queue's head. */
		if (priv->queue != NULL && g_queue_get_length (priv->queue) > 0) {
			g_warn_if_fail (priv->current != NULL);
			message = priv->current;
			priv->current = g_queue_pop_head (priv->queue);
		} else {
			message = priv->current;
			priv->current = NULL;
		}

		if (message == NULL) {
			/* Unpause & quit flushing now the queue is empty */
			flag_was_set = g_atomic_int_compare_and_exchange (&priv->paused, TRUE, FALSE);
			g_warn_if_fail (flag_was_set);
			break;
		}

		/* Unlock while we can so we don't block threads that want to post to
		 * the port. FIXME: is there a more effecient way?
		 */
		g_mutex_unlock (priv->mutex);

		delivered = iris_receiver_deliver (receiver, message);

		g_mutex_lock (priv->mutex);

		if (delivered == IRIS_DELIVERY_REMOVE)
			store_message_at_head_ul (port, message);

		if (delivered == IRIS_DELIVERY_REMOVE || delivered == IRIS_DELIVERY_ACCEPTED_REMOVE)
			g_atomic_pointer_compare_and_exchange ((gpointer *)&priv->receiver, receiver, NULL);

		if (delivered == IRIS_DELIVERY_PAUSE) {
			/* Try again. Pass TRUE so if delivery is deferred, the item goes
			 * back to the head of the queue not the tail
			 */
			delivered = post_with_lock_ul (port, receiver, message, TRUE);
		}

		/* Free the reference that the queue held; the message has either been
		 * requeued (with a new reference) or taken by the receiver
		 */
		iris_message_unref (message);

		/* Only continue flushing if the receiver is still accepting */
	} while (delivered == IRIS_DELIVERY_ACCEPTED);

	flag_was_set = g_atomic_int_compare_and_exchange (&priv->flushing, TRUE, FALSE);
	g_warn_if_fail (flag_was_set);

	/* Don't free the queue even if it's empty. We will probably need it again. */

	g_mutex_unlock (priv->mutex);
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
	return PORT_IS_PAUSED (port);
}
