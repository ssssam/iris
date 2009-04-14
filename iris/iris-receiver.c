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
#include "iris-receiver.h"
#include "iris-receiver-private.h"

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

G_DEFINE_TYPE (IrisReceiver, iris_receiver, G_TYPE_OBJECT);

typedef struct
{
	IrisReceiverPrivate *priv;
	IrisMessage         *message;
} IrisWorkerData;

static void
iris_receiver_worker_cb (gpointer data)
{
	IrisWorkerData *worker;

	g_return_if_fail (data != NULL);

	worker = data;

	if (g_atomic_int_dec_and_test (&worker->priv->active)) {}

	iris_message_unref (worker->message);

	g_slice_free (IrisWorkerData, worker);
}

static void
iris_receiver_worker (gpointer data)
{
	IrisWorkerData *worker;

	g_return_if_fail (data != NULL);

	worker = data;

	if (G_LIKELY (worker->priv->callback))
		worker->priv->callback (worker->message,
		                        worker->priv->data);
}

static IrisDeliveryStatus
iris_receiver_deliver_real (IrisReceiver *receiver,
                            IrisMessage  *message)
{
	IrisReceiverPrivate *priv;
	IrisDeliveryStatus   status;
	IrisReceiveDecision  decision;
	gboolean             execute;
	IrisWorkerData      *worker;

	g_return_val_if_fail (message != NULL, IRIS_DELIVERY_REMOVE);

	priv = receiver->priv;

	g_mutex_lock (priv->mutex);

	if (g_atomic_int_get (&priv->completed) == TRUE) {
		decision = IRIS_DELIVERY_REMOVE;
		execute = FALSE;
	}
	else if ((priv->max_active > 0 && priv->active == priv->max_active)
	         || g_atomic_pointer_get (&priv->message))
	{
		/* We cannot accept an item at this time, so let
		 * the port queue the item for us.
		 */
		decision = IRIS_DELIVERY_PAUSE;
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
			/* We queue this item ourselves */
			execute = FALSE;
			priv->message = iris_message_ref (message);
			status = IRIS_DELIVERY_ACCEPTED_PAUSE;
			break;
		case IRIS_RECEIVE_NEVER:
			/* The port should queue the item and remove us */
			execute = FALSE;
			status = IRIS_DELIVERY_REMOVE;
			break;
		default:
			g_assert_not_reached ();
		}
	}
	else {
		execute = TRUE;
		status = IRIS_DELIVERY_ACCEPTED;
	}

	/* We do this before leaving the lock to prevent a potential
	 * race condition where we could go over our max concurrent.
	 */
	if (execute)
		g_atomic_int_inc (&priv->active);

	/* If our execution will be the only execution allowed, so make
	 * sure that we mark the receiver as it is completed.
	 */
	if (!priv->persistent && execute)
		g_atomic_int_set (&priv->completed, TRUE);

	g_mutex_unlock (priv->mutex);

	if (execute) {
		worker = g_slice_new0 (IrisWorkerData);
		worker->priv = priv;
		worker->message = message;

		iris_scheduler_queue (priv->scheduler,
		                      iris_receiver_worker,
		                      worker,
		                      iris_receiver_worker_cb);

		if (!priv->persistent)
			status = IRIS_DELIVERY_ACCEPTED_REMOVE;
	}

	return status;
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

	klass->deliver = iris_receiver_deliver_real;
	object_class->finalize = iris_receiver_finalize;

	g_type_class_add_private (object_class, sizeof (IrisReceiverPrivate));
}

static void
iris_receiver_init (IrisReceiver *receiver)
{
	receiver->priv = G_TYPE_INSTANCE_GET_PRIVATE (receiver,
	                                              IRIS_TYPE_RECEIVER,
	                                              IrisReceiverPrivate);

	receiver->priv->mutex = g_mutex_new ();
	receiver->priv->persistent = TRUE;
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
 * iris_receiver_new_full:
 * @scheduler: An #IrisScheduler
 * @arbiter: An #IrisArbiter
 * @callback: a callback to execute
 * @data: user data for the callback
 *
 * Creates a new instance of #IrisReceiver.  The receiver is initialized
 * to dispatch work items to @scheduler.  Messages can only be dispatched
 * if the arbiter allows us to.
 *
 * Return value: The newly created #IrisReceiver instance.
 */
IrisReceiver*
iris_receiver_new_full (IrisScheduler      *scheduler,
                        IrisArbiter        *arbiter,
                        IrisMessageHandler  callback,
                        gpointer            data)
{
	IrisReceiver        *receiver;
	IrisReceiverPrivate *priv;

	g_return_val_if_fail (IRIS_IS_SCHEDULER (scheduler), NULL);
	g_return_val_if_fail (arbiter == NULL || IRIS_IS_ARBITER (arbiter), NULL);

	receiver = iris_receiver_new ();
	priv = receiver->priv;

	priv->scheduler = g_object_ref (scheduler);
	priv->arbiter = arbiter ? g_object_ref (arbiter) : NULL;
	priv->callback = callback;
	priv->data = data;

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
