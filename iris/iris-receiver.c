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
 * You should use iris_receiver_destroy() to finalize an #IrisReceiver.
 *
 * #IrisReceiver objects can be attached to an arbiter to provide
 * additional control over when actions can be performed.  See
 * iris_arbiter_coordinate() for how to use the Coordination Arbiter.
 * It provides a feature similar to a ReaderWriter lock using an
 * asynchronous model.
 */

G_DEFINE_TYPE (IrisReceiver, iris_receiver, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_SCHEDULER,
};

typedef struct
{
	gboolean      executed;
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
	IrisWorkerData *worker = data;

	if (!worker->executed)
		if (g_atomic_int_dec_and_test (&worker->receiver->priv->active)) { };

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

	/* It's possible that the message could cause the destruction of the
	 * owner of this receiver and thus a call to iris_receiver_destroy().
	 */
	g_object_ref (worker->receiver);

	worker->executed = TRUE;

	/* Execute the callback */
	priv->callback (worker->message, priv->data);

	/* Decrement before we notify the arbiter so it will always notice if
	 * priv->active==0 and call iris_receiver_resume(). We could be even more
	 * atomic and do dec_and_test() inside the arbiter, but it's not actually
	 * necessary.
	 */
	if (g_atomic_int_dec_and_test (&priv->active)) { }

	/* Protect against destruction in this phase: since we have already
	 * decremented priv->active, iris_receiver_destroy() doesn't know we
	 * are still executing. Feel free to implement this in a faster way.
	 */
	g_static_rec_mutex_lock (&priv->destroy_mutex);

	if (priv->port == NULL);
		/* iris_receiver_destroy() has been called */
	else
		/* Notify the arbiter we are complete. */
		if (priv->arbiter)
			iris_arbiter_receive_completed (priv->arbiter, worker->receiver);

	g_static_rec_mutex_unlock (&priv->destroy_mutex);

	g_object_unref (worker->receiver);
}

static IrisDeliveryStatus
iris_receiver_deliver_real (IrisReceiver *receiver,
                            IrisMessage  *message)
{
	IrisReceiverPrivate *priv;
	IrisDeliveryStatus   status = IRIS_DELIVERY_PAUSE;
	IrisReceiveDecision  decision;
	gboolean             execute = TRUE;
	IrisWorkerData      *worker;

	g_return_val_if_fail (message != NULL, IRIS_DELIVERY_ACCEPTED);

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
	else if (priv->max_active > 0 && g_atomic_int_get (&priv->active) == priv->max_active)
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
			break;
		case IRIS_RECEIVE_LATER:
			/* Port must queue this */
			execute = FALSE;
			status = IRIS_DELIVERY_PAUSE;
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
		if (!priv->persistent)
			status = IRIS_DELIVERY_ACCEPTED_REMOVE;

		worker = g_slice_new0 (IrisWorkerData);
		worker->receiver = receiver;
		worker->executed = FALSE;
		worker->message = iris_message_ref_sink (message);

		iris_scheduler_queue (priv->scheduler,
		                      iris_receiver_worker,
		                      worker,
		                      iris_receiver_worker_destroy_cb);
	}

	return status;
}


static void
iris_receiver_constructed (GObject *object)
{
	IrisReceiver        *receiver;
	IrisReceiverPrivate *priv;

	/* Chaining up to GObject was broken until GObject 2.27.93 */
	/*G_OBJECT_CLASS (iris_receiver_parent_class)->constructed (object); */

	receiver = IRIS_RECEIVER (object);
	priv = receiver->priv;

	/* This construct-only properties will have been set to the default by
	 * by set_property() if not explicitly set on construct.
	 */
	g_warn_if_fail (priv->scheduler != NULL);
}

static void
iris_receiver_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	IrisReceiver        *receiver;
	IrisReceiverPrivate *priv;
	IrisScheduler       *scheduler;

	receiver = IRIS_RECEIVER (object);
	priv = receiver->priv;

	switch (prop_id) {
		/* Construct-only property */
		case PROP_SCHEDULER:
			g_warn_if_fail (priv->scheduler == NULL);

			scheduler = g_value_get_object (value);
			if (scheduler == NULL)
				scheduler = iris_get_default_control_scheduler ();

			priv->scheduler = g_object_ref (scheduler);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
iris_receiver_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	IrisReceiver        *receiver;
	IrisReceiverPrivate *priv;

	receiver = IRIS_RECEIVER (object);
	priv = receiver->priv;

	switch (prop_id) {
		/* Construct-only properties */
		case PROP_SCHEDULER:
			g_value_set_object (value, priv->scheduler);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
iris_receiver_dispose (GObject *object)
{
	IrisReceiverPrivate *priv;

	g_return_if_fail (IRIS_IS_RECEIVER (object));

	priv = IRIS_RECEIVER (object)->priv;

	/* Protect against double emission */
	if (priv->scheduler != NULL) {
		/* We can't be being disposed if a port still holds a reference */
		g_warn_if_fail (priv->port == NULL);

		g_object_unref (priv->scheduler);
		priv->scheduler = NULL;

		if (priv->notify)
			priv->notify (priv->data);
	}

	G_OBJECT_CLASS (iris_receiver_parent_class)->dispose (object);
}

static void
iris_receiver_finalize (GObject *object)
{
	IrisReceiverPrivate *priv;

	priv = IRIS_RECEIVER (object)->priv;

	if (g_atomic_int_get (&priv->active) > 0)
		g_warning ("receiver %lx was finalized with messages still active. "
		           "This is likely to cause a crash. Always use "
		           "iris_receiver_destroy() to free an IrisReceiver.",
		           (gulong)object);

	G_OBJECT_CLASS (iris_receiver_parent_class)->finalize (object);
}

static void
iris_receiver_class_init (IrisReceiverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	klass->deliver = iris_receiver_deliver_real;
	object_class->constructed = iris_receiver_constructed;
	object_class->set_property = iris_receiver_set_property;
	object_class->get_property = iris_receiver_get_property;
	object_class->dispose = iris_receiver_dispose;
	object_class->finalize = iris_receiver_finalize;

	/**
	 * IrisReceiver:scheduler:
	 *
	 * The #IrisScheduler used to deliver messages, ie. where the message
	 * handler function is called from.
	 */
	g_object_class_install_property
	  (object_class,
	   PROP_SCHEDULER,
	   g_param_spec_object ("scheduler",
	                        "Scheduler",
	                        "Scheduler used to deliver messages",
	                        IRIS_TYPE_SCHEDULER,
	                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME |
	                        G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (IrisReceiverPrivate));
}

static void
iris_receiver_init (IrisReceiver *receiver)
{
	receiver->priv = G_TYPE_INSTANCE_GET_PRIVATE (receiver,
	                                              IRIS_TYPE_RECEIVER,
	                                              IrisReceiverPrivate);

	g_static_rec_mutex_init (&receiver->priv->mutex);
	g_static_rec_mutex_init (&receiver->priv->destroy_mutex);
	receiver->priv->persistent = TRUE;
}

/*
 * iris_receiver_deliver:
 * @receiver: An #IrisReceiver
 * @message: An #IrisMessage
 *
 * Delivers a message to the receiver so that the receiver may take an
 * action on the message. Used internally by #IrisPort.
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

/*
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

	iris_debug (IRIS_DEBUG_RECEIVER);

	g_return_if_fail (IRIS_IS_RECEIVER (receiver));

	priv = receiver->priv;

	/* We have been destroyed, shouldn't have got here! */
	g_return_if_fail (priv->port != NULL);

	iris_port_resume (priv->port);
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

	iris_scheduler_unqueue (scheduler, work_item);

	return (g_atomic_int_get (&priv->active) > 0);
}

/**
 * iris_receiver_destroy:
 * @receiver: An #IrisReceiver
 * @in_message: Pass %TRUE if this function was called from the message handler
 *              of @receiver itself.
 *
 * Disconnects the port connected to @receiver, cancels any messages that are
 * still pending and frees @receiver. Note that any or all of the messages may
 * still execute before the function returns, so you should only begin
 * destruction of shared data once this function completes.
 *
 * iris_receiver_destroy() involves flushing queued events from the scheduler
 * and making sure all messages have completed. If you call the function from
 * the receiver's own message handler you must pass %TRUE to @in_message so
 * @receiver knows not to wait for this message to complete.
 */
/* FIXME: it might be nice if we could set a flag on 'port' so that the owner
 * knows (if they had no other way of knowing) that the communication channel
 * has closed and they should stop sending messages and unref the port.
 */
void
iris_receiver_destroy (IrisReceiver *receiver,
                       gboolean      in_message)
{
	IrisReceiverPrivate *priv;
	gint                 max_messages;

	g_return_if_fail (IRIS_IS_RECEIVER (receiver));

	priv = receiver->priv;

	if (in_message)
		g_warn_if_fail (g_atomic_int_get (&priv->active) >= 1);

	g_static_rec_mutex_lock (&priv->destroy_mutex);

	/* Close off the port to avoid getting more messages
	 * (and release its reference)
	 */
	iris_port_set_receiver (priv->port, NULL);
	g_object_unref (priv->port);
	priv->port = NULL;

	/* Unqueue any callbacks still queued. */
	max_messages = in_message? 1: 0;
	while (g_atomic_int_get (&priv->active) > max_messages) {
		iris_scheduler_foreach (priv->scheduler,
		                        iris_receiver_worker_unqueue_cb,
		                        receiver);

		/* HANG ALERT! iris_scheduler_unqueue() does not abort work items that are
		 * already running, so here we wait for these to complete.
		 */

		IRIS_SCHEDULER_GET_CLASS (priv->scheduler)->iterate (priv->scheduler);
	}

	g_static_rec_mutex_unlock (&priv->destroy_mutex);

	if (in_message) {
		/* If we were in our own message the worker must still be executing
		 * this message, and must still hold a reference
		 */
		g_warn_if_fail (g_atomic_int_get (&priv->active) == 1);
		g_warn_if_fail (G_OBJECT (receiver)->ref_count >= 2);
	}

	/* Remove arbiter */
	if (priv->arbiter != NULL) {
		g_warn_if_fail (G_OBJECT(priv->arbiter)->ref_count == 1);
		g_object_unref (priv->arbiter);
		priv->arbiter = NULL;
	};

	g_object_run_dispose (G_OBJECT (receiver));

	/* Object may be freed now, or there could be a reference still held while
	 * iris_receiver_worker() completes if a message triggered our own
	 * destruction
	 */
	g_object_unref (receiver);
}

/*
 * iris_receiver_has_arbiter:
 * @receiver: An #IrisReceiver
 *
 * Private, used by unit tests and internally only.
 *
 * Determines if the receiver currently has an arbiter attached.
 *
 * Return value: %TRUE if an arbiter exists.
 */
gboolean
iris_receiver_has_arbiter (IrisReceiver *receiver)
{
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), FALSE);

	priv = receiver->priv;

	return g_atomic_pointer_get (&priv->arbiter) != NULL;
}
