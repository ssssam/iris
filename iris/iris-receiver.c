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

#include "iris-arbiter.h"
#include "iris-arbiter-private.h"
#include "iris-debug.h"
#include "iris-receiver.h"
#include "iris-receiver-private.h"
#include "iris-port.h"

/**
 * SECTION:iris-receiver
 * @title: IrisReceiver
 * @short_description: Perform actions upon message delivery
 *
 * #IrisReceiver is used to perform actions when a message is received
 * from an #IrisPort.  See iris_arbiter_receive() for how to create
 * a new receiver for an #IrisPort to perform a given action.
 *
 * Before an #IrisReceiver is destroyed it must be closed, using
 * iris_receiver_close(). This is to avoid messages that are queued in the
 * scheduler running after the destruction of the receiver.
 *
 * #IrisReceiver objects can be attached to an arbiter to provide
 * additional control over when actions can be performed.  See
 * iris_arbiter_coordinate() for how to use the Coordination Arbiter.
 * It provides a feature similar to a ReaderWriter lock using an
 * asynchronous model.
 */

G_DEFINE_TYPE (IrisReceiver, iris_receiver, G_TYPE_OBJECT)

typedef struct
{
	IrisReceiver *receiver;
	IrisMessage  *message;
} IrisWorkerData;

GType
iris_delivery_status_get_type (void)
{
	static GType      gtype    = 0;
	static GEnumValue values[] = {
		{ IRIS_DELIVERY_ACCEPTED,        "IRIS_DELIVERY_ACCEPTED",        "ACCEPTED" },
		{ IRIS_DELIVERY_PAUSE,           "IRIS_DELIVERY_PAUSE",           "PAUSE" },
		{ IRIS_DELIVERY_ACCEPTED_PAUSE,  "IRIS_DELIVERY_ACCEPTED_PAUSE",  "ACCEPTED_PAUSE" },
		{ IRIS_DELIVERY_REMOVE,          "IRIS_DELIVERY_REMOVE",          "REMOVE" },
		{ IRIS_DELIVERY_ACCEPTED_REMOVE, "IRIS_DELIVERY_ACCEPTED_REMOVE", "ACCEPTED_REMOVE" },
		{ 0, NULL, NULL }
	};

	if (G_UNLIKELY (!gtype))
		gtype = g_enum_register_static ("IrisDeliveryStatus", values);

	return gtype;
}

static void
iris_receiver_worker_destroy_cb (gpointer data)
{
	IrisReceiverPrivate *priv;
	IrisWorkerData      *worker;

	worker = data;
	priv = worker->receiver->priv;

	iris_message_unref (worker->message);
	g_slice_free (IrisWorkerData, worker);
}

static void
iris_receiver_worker (gpointer data)
{
	IrisReceiverPrivate *priv;
	IrisWorkerData      *worker;

	g_return_if_fail (data != NULL);

	worker = data;
	priv = worker->receiver->priv;

	/* Execute the callback */
	priv->callback (worker->message, priv->data);

	/* notify the arbiter we are complete */
	if (priv->arbiter)
		iris_arbiter_receive_completed (priv->arbiter,
		                                worker->receiver);

	if (g_atomic_int_dec_and_test (&priv->active)) { }
}

static IrisDeliveryStatus
iris_receiver_deliver_real (IrisReceiver *receiver,
                            IrisMessage  *message)
{
	IrisReceiverPrivate *priv;
	IrisDeliveryStatus   status = IRIS_DELIVERY_PAUSE;
	IrisReceiveDecision  decision;
	gboolean             execute = TRUE;
	gboolean             unpause = FALSE;
	IrisWorkerData      *worker;

	g_return_val_if_fail (message != NULL, IRIS_DELIVERY_REMOVE);

	priv = receiver->priv;

	/* arbiter cannot be changed after instantiation, so it is safe to
	 * check the arbiter pointer with out a lock or memory barrier.
	 * Without an arbiter, we cannot pause, so we can assume that the
	 * flood gates are open if we do not have a max allowed or an
	 * arbiter. (Fast-Path)
	 */
	if (!priv->arbiter && !priv->max_active) {
		execute = TRUE;
		status = IRIS_DELIVERY_ACCEPTED;

		/* Same code as below but outside the mutex, because without an arbiter
		 * there's no need.
		 */
		if (!priv->persistent && execute)
			if (!g_atomic_int_compare_and_exchange (&priv->completed, FALSE, TRUE))
				execute = FALSE;

		if (execute)
			g_atomic_int_inc (&priv->active);

		goto _post_decision;
	}


	g_static_rec_mutex_lock (&priv->mutex);

	if (g_atomic_int_get (&priv->completed) == TRUE) {
		status = IRIS_DELIVERY_REMOVE;
		execute = FALSE;
	}
	else if ((priv->max_active > 0 && g_atomic_int_get (&priv->active) == priv->max_active)
	          || priv->message)
	{
		/* We cannot accept an item at this time, so let
		 * the port queue the item for us.
		 */
		status = IRIS_DELIVERY_PAUSE;
		execute = FALSE;
	}
	else if (priv->arbiter) {
		decision = iris_arbiter_can_receive (priv->arbiter, receiver);

		switch (decision) {
		case IRIS_RECEIVE_NOW:
			/* We can execute this now */
			execute = TRUE;
			status = IRIS_DELIVERY_ACCEPTED;
			/* We also need to unpause the port */
			unpause = TRUE;
			break;
		case IRIS_RECEIVE_LATER:
			/* We queue this item ourselves */
			execute = FALSE;
			g_warn_if_fail (priv->message == NULL);
			priv->message = iris_message_ref (message);
			status = IRIS_DELIVERY_ACCEPTED_PAUSE;
			break;
		case IRIS_RECEIVE_NEVER:
			/* The port should queue the item and remove us */
			execute = FALSE;
			status = IRIS_DELIVERY_REMOVE;
			break;
		default:
			g_warn_if_reached ();
			break;
		}
	}


	/* If our execution will be the only execution allowed, so make
	 * sure that we mark the receiver as it is completed.  Also, we
	 * need to compare exchange just incase we race and lose.  This
	 * could happen if we are not persistent and do not have an arbiter,
	 * which means we avoid the lock above.
	 */
	if (!priv->persistent && execute)
		if (!g_atomic_int_compare_and_exchange (&priv->completed, FALSE, TRUE))
			execute = FALSE;

	/* We do this before leaving the lock to prevent a potential
	 * race condition where we could go over our max concurrent.
	 */
	if (execute)
		g_atomic_int_inc (&priv->active);

	g_static_rec_mutex_unlock (&priv->mutex);

_post_decision:

	if (execute) {
		worker = g_slice_new0 (IrisWorkerData);
		worker->receiver = receiver;
		worker->message = iris_message_ref (message);

		iris_scheduler_queue (priv->scheduler,
		                      iris_receiver_worker,
		                      worker,
		                      iris_receiver_worker_destroy_cb);

		if (!priv->persistent)
			status = IRIS_DELIVERY_ACCEPTED_REMOVE;
	}

	if (unpause && iris_port_is_paused (priv->port)) {
		iris_port_flush (priv->port, NULL);
	}

	return status;
}

static void
iris_receiver_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_receiver_parent_class)->finalize (object);
}

static void
iris_receiver_dispose (GObject *object)
{
	IrisReceiverPrivate *priv;

	g_return_if_fail (IRIS_IS_RECEIVER (object));

	priv = IRIS_RECEIVER (object)->priv;

	/* We can't be being disposed if a port still holds a reference */
	g_warn_if_fail (priv->port != NULL);

	if (g_atomic_int_get (&priv->active) > 0)
		g_warning ("receiver %lx was finalized with messages still active. "
		           "This is likely to cause a crash. The owner of an "
		           "IrisReceiver must always call iris_receiver_close() "
		           "before unreferencing the receiver.", (gulong)object);

	g_object_unref (priv->scheduler);

	if (priv->notify)
		priv->notify (priv->data);

	G_OBJECT_CLASS (iris_receiver_parent_class)->dispose (object);
}

static void
iris_receiver_class_init (IrisReceiverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->deliver = iris_receiver_deliver_real;
	object_class->finalize = iris_receiver_finalize;
	object_class->dispose = iris_receiver_dispose;

	g_type_class_add_private (object_class, sizeof (IrisReceiverPrivate));
}

static void
iris_receiver_init (IrisReceiver *receiver)
{
	receiver->priv = G_TYPE_INSTANCE_GET_PRIVATE (receiver,
	                                              IRIS_TYPE_RECEIVER,
	                                              IrisReceiverPrivate);

	g_static_rec_mutex_init (&receiver->priv->mutex);
	receiver->priv->persistent = TRUE;
}

/**
 * iris_receiver_deliver:
 * @receiver: An #IrisReceiver
 * @message: An #IrisMessage
 *
 * Delivers a message to the receiver so that the receiver may take an
 * action on the message.
 *
 * Return value: the status code for the delivery.
 */
IrisDeliveryStatus
iris_receiver_deliver (IrisReceiver *receiver,
                       IrisMessage  *message)
{
	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), IRIS_DELIVERY_REMOVE);
	return IRIS_RECEIVER_GET_CLASS (receiver)->deliver (receiver, message);
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

/**
 * iris_receiver_get_scheduler:
 * @receiver: An #IrisReceiver
 *
 * Retrieves the scheduler instance for the receiver.
 *
 * Return value: An #IrisScheduler instance
 */
IrisScheduler*
iris_receiver_get_scheduler (IrisReceiver *receiver)
{
	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), NULL);
	return g_atomic_pointer_get (&receiver->priv->scheduler);
}

/**
 * iris_receiver_set_scheduler:
 * @receiver: An #IrisReceiver
 * @scheduler: An #IrisScheduler
 *
 * Sets the scheduler instance used by this receiver to execute work items.
 * Note that it is probably not a good idea to switch schedulers while
 * executing work items.  However, we do make an attempt to support it.
 */
void
iris_receiver_set_scheduler (IrisReceiver  *receiver,
                             IrisScheduler *scheduler)
{
	IrisScheduler *old_sched;

	g_return_if_fail (IRIS_IS_RECEIVER (receiver));
	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	scheduler = g_object_ref (scheduler);

	do {
		old_sched = iris_receiver_get_scheduler (receiver);
	} while (!g_atomic_pointer_compare_and_exchange (
				(gpointer*)&receiver->priv->scheduler,
				old_sched,
				scheduler));

	g_object_unref (old_sched);
}

/**
 * iris_receiver_resume:
 * @receiver: An #IrisReceiver
 *
 * Resumes delivery of a port. Any pending messages in the receiver
 * will attempt to redeliver.  The port attached to the receiver will
 * also be flushed.
 */
void
iris_receiver_resume (IrisReceiver *receiver)
{
	IrisReceiverPrivate *priv;
	IrisMessage         *message = NULL;

	iris_debug (IRIS_DEBUG_RECEIVER);

	g_return_if_fail (IRIS_IS_RECEIVER (receiver));

	priv = receiver->priv;

	g_static_rec_mutex_lock (&priv->mutex);

	if (priv->message) {
		message = priv->message;
		priv->message = NULL;
	}

	g_static_rec_mutex_unlock (&priv->mutex);

	/* Our held message is posted at the front of the queue */
	iris_port_flush (priv->port, message);

	if (message != NULL)
		iris_message_unref (message);
}


static gboolean
iris_receiver_worker_unqueue_cb (IrisScheduler *scheduler,
                                 gpointer       work_item,
                                 IrisCallback   callback,
                                 gpointer       data,
                                 gpointer       user_data)
{
	IrisWorkerData      *worker_data;
	IrisReceiver        *receiver;
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_RECEIVER (user_data), FALSE);

	receiver = IRIS_RECEIVER (user_data);
	priv = receiver->priv;

	if (callback != iris_receiver_worker)
		return TRUE;

	worker_data = data;

	if (worker_data->receiver != receiver)
		return TRUE;

	if (iris_scheduler_unqueue (scheduler, work_item))
		/* 'unqueue' returns TRUE if the item did not execute */
		if (g_atomic_int_dec_and_test (&priv->active)) { }

	return (g_atomic_int_get (&priv->active) > 0);
}

/**
 * iris_receiver_close:
 * @receiver: An #IrisReceiver
 * @main_context: A #GMainContext (if %NULL, the default context will be used)
 * @iterate_main_context: Whether to run @main_context while waiting for the
 *                        messages to process (default: %FALSE).
 *
 * Disconnects the port connected to @receiver and cancels any messages that
 * are still pending. Note that any or all of the messages may still execute
 * before the function returns, so you should only begin destruction of shared
 * data once this function completes. This function must be called by the owner
 * of @receiver before it is unreferenced, to prevent messages executing after
 * the receiver has been destroyed.
 *
 * Because iris_receiver_close() involves flushing queued events from the
 * scheduler, users of the GLib main loop may need to pass their application's
 * #GMainContext (%NULL for the default one) as @main_context and %TRUE to
 * @iterate_main_context. This is necessary when you are calling
 * iris_receiver_close() from the main loop thread <emphasis>and</emphasis> any
 * of the following are true:
 * <itemizedlist>
 * <listitem>@receiver's scheduler is an #IrisGMainScheduler running
 *           in the same main loop</listitem>
 * <listitem>The message handling callback for @receiver may take a long time
 *           to execute and you would like to keep processing other event
 *           sources (such as a GUI)</listitem>
 * <listitem>The message handling callback for @receiver depends on the main
 *           loop in some way (although that would be weird)</listitem>
 * </itemizedlist>
 */
/* FIXME: it might be nice if we could set a flag on 'port' so that the owner
 * knows (if they had no other way of knowing) that the communication channel
 * has closed and they should stop sending messages and unref the port.
 */
void
iris_receiver_close (IrisReceiver *receiver,
                     GMainContext *main_context,
                     gboolean      iterate_main_context)
{
	IrisReceiverPrivate *priv;

	g_return_if_fail (IRIS_IS_RECEIVER (receiver));

	priv = receiver->priv;

	/* Close off the port to avoid getting more messages */
	iris_port_set_receiver (priv->port, NULL);

	/* Unqueue any callbacks still queued. */
	while (g_atomic_int_get (&priv->active) > 0) {
		iris_scheduler_foreach (priv->scheduler,
		                        iris_receiver_worker_unqueue_cb,
		                        receiver);

		/* HANG ALERT! iris_scheduler_unqueue() does not abort work items that are
		 * already running, so here we wait for these to complete.
		 */

		g_thread_yield ();

		if (iterate_main_context)
			g_main_context_iteration (main_context, FALSE);
	}
}
