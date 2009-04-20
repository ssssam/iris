/* iris-coordination-arbiter.c
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
#include "iris-coordination-arbiter.h"
#include "iris-coordination-arbiter-private.h"
#include "iris-port.h"
#include "iris-receiver.h"
#include "iris-receiver-private.h"

#define ATTACH_ARBITER(r,a)                                      \
	G_STMT_START {                                           \
		if (r && !r->priv->arbiter)                      \
			r->priv->arbiter = g_object_ref (a);     \
	} G_STMT_END
#define FLAG_IS_ON(p,f)   ((p->flags & (f)) != 0)
#define FLAG_IS_OFF(p,f)  ((p->flags & (f)) == 0)
#define ENABLE_FLAG(p,f)  p->flags |= (f)
#define DISABLE_FLAG(p,f) p->flags &= ~(f)
#define NEEDS_SWITCH(p)                                         \
	((p->flags & (IRIS_COORD_NEEDS_EXCLUSIVE  |             \
	              IRIS_COORD_NEEDS_CONCURRENT |             \
	              IRIS_COORD_NEEDS_TEARDOWN)) != 0)

/**
 * SECTION:iris-coordination-receiver
 * @short_description: #IrisArbiter to manage exclusive vs concurrent messages
 *
 * The #IrisCoordinationArbiter provides management over how incoming messages
 * can be handled.  Its primary purpose is to allow messages to be as
 * concurrent as possible until an exclusive message is received.  When that
 * happens, it will bleed off the concurrent messages and then run the
 * exclusive messages. After the exclusive messages have processed, the flood
 * gates can re-open and throttle back up to full concurrency.
 */

G_DEFINE_TYPE (IrisCoordinationArbiter,
               iris_coordination_arbiter,
               IRIS_TYPE_ARBITER);

static IrisReceiveDecision
iris_coordination_arbiter_can_receive (IrisArbiter  *arbiter,
                                       IrisReceiver *receiver)
{
	IrisCoordinationArbiterPrivate *priv;
	gboolean                        can_switch;
	IrisReceiver                   *active_receiver;
	IrisReceiver                   *resume = NULL;
	IrisReceiveDecision             decision;

	g_return_val_if_fail (IRIS_IS_COORDINATION_ARBITER (arbiter), IRIS_RECEIVE_NEVER);
	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), IRIS_RECEIVE_NEVER);

	priv = IRIS_COORDINATION_ARBITER (arbiter)->priv;

	g_mutex_lock (priv->mutex);

	if (FLAG_IS_ON (priv, IRIS_COORD_TEARDOWN)) {
		decision = IRIS_RECEIVE_NEVER;
		goto _unlock;
	}

	can_switch = (priv->active == 0);

	if (FLAG_IS_ON (priv, IRIS_COORD_EXCLUSIVE))
		active_receiver = priv->exclusive;
	else if (FLAG_IS_ON (priv, IRIS_COORD_CONCURRENT))
		active_receiver = priv->concurrent;
	else
		g_assert_not_reached ();

	if (!NEEDS_SWITCH (priv)) {
		if (receiver == active_receiver && (receiver != priv->exclusive || !priv->active)) {
			decision = IRIS_RECEIVE_NOW;
			goto _unlock;
		}
		else if (!can_switch) {
			if (receiver == priv->exclusive)
				ENABLE_FLAG (priv, IRIS_COORD_NEEDS_EXCLUSIVE);
			else if (receiver == priv->concurrent)
				ENABLE_FLAG (priv, IRIS_COORD_NEEDS_CONCURRENT);
			else if (receiver == priv->teardown)
				ENABLE_FLAG (priv, IRIS_COORD_NEEDS_TEARDOWN);
			else
				g_assert_not_reached ();
			decision = IRIS_RECEIVE_LATER;
		}
		else {
			DISABLE_FLAG (priv, IRIS_COORD_EXCLUSIVE
			                  | IRIS_COORD_CONCURRENT
			                  | IRIS_COORD_TEARDOWN);
			if (receiver == priv->exclusive) {
				ENABLE_FLAG (priv, IRIS_COORD_EXCLUSIVE);
				DISABLE_FLAG (priv, IRIS_COORD_NEEDS_EXCLUSIVE);
			}
			else if (receiver == priv->concurrent) {
				ENABLE_FLAG (priv, IRIS_COORD_CONCURRENT);
				DISABLE_FLAG (priv, IRIS_COORD_NEEDS_CONCURRENT);
			}
			else if (receiver == priv->teardown) {
				ENABLE_FLAG (priv, IRIS_COORD_TEARDOWN);
				DISABLE_FLAG (priv, IRIS_COORD_NEEDS_TEARDOWN);
			}
			decision = IRIS_RECEIVE_NOW;
		}
	}
	else if (can_switch) {
		if (receiver == priv->exclusive) {
			DISABLE_FLAG (priv, IRIS_COORD_NEEDS_EXCLUSIVE);
			ENABLE_FLAG (priv, IRIS_COORD_EXCLUSIVE);
			resume = priv->exclusive;
		}
		else if (receiver == priv->concurrent) {
			DISABLE_FLAG (priv, IRIS_COORD_NEEDS_CONCURRENT);
			ENABLE_FLAG (priv, IRIS_COORD_CONCURRENT);
			resume = priv->concurrent;
		}
		else if (receiver == priv->teardown) {
			DISABLE_FLAG (priv, IRIS_COORD_NEEDS_TEARDOWN);
			ENABLE_FLAG (priv, IRIS_COORD_TEARDOWN);
			resume = priv->teardown;
		}
		else
			g_assert_not_reached ();
		decision = IRIS_RECEIVE_NOW;

	}
	else {
		decision = IRIS_RECEIVE_LATER;
		if (receiver == priv->exclusive)
			ENABLE_FLAG (priv, IRIS_COORD_NEEDS_EXCLUSIVE);
		else if (receiver == priv->concurrent)
			ENABLE_FLAG (priv, IRIS_COORD_NEEDS_CONCURRENT);
		else if (receiver == priv->teardown)
			ENABLE_FLAG (priv, IRIS_COORD_NEEDS_TEARDOWN);
		else
			g_assert_not_reached ();
	}

_unlock:
	if (decision == IRIS_RECEIVE_NOW)
		g_atomic_int_inc ((gint*)&priv->active);

	g_mutex_unlock (priv->mutex);

	if (resume)
		iris_receiver_resume (resume);

	return decision;
}

static void
iris_coordination_arbiter_receive_completed (IrisArbiter  *arbiter,
                                             IrisReceiver *receiver)
{
	IrisCoordinationArbiterPrivate *priv;
	IrisReceiver                   *resume = NULL;

	g_return_if_fail (IRIS_IS_COORDINATION_ARBITER (arbiter));
	g_return_if_fail (IRIS_IS_RECEIVER (receiver));

	priv = IRIS_COORDINATION_ARBITER (arbiter)->priv;

	g_mutex_lock (priv->mutex);

	if (G_UNLIKELY (g_atomic_int_dec_and_test ((int*)&priv->active))) {
		if (NEEDS_SWITCH (priv)) {
			if (FLAG_IS_ON (priv, IRIS_COORD_NEEDS_EXCLUSIVE)) {
				DISABLE_FLAG (priv, IRIS_COORD_NEEDS_EXCLUSIVE | IRIS_COORD_CONCURRENT);
				ENABLE_FLAG (priv, IRIS_COORD_EXCLUSIVE);
				resume = priv->exclusive;
			}
			else if (FLAG_IS_ON (priv, IRIS_COORD_NEEDS_CONCURRENT)) {
				DISABLE_FLAG (priv, IRIS_COORD_NEEDS_CONCURRENT | IRIS_COORD_EXCLUSIVE);
				ENABLE_FLAG (priv, IRIS_COORD_CONCURRENT);
				resume = priv->concurrent;
			}
			else if (FLAG_IS_ON (priv, IRIS_COORD_NEEDS_TEARDOWN)) {
				/* NOTE: This might needs to go first to prevent
				 *   starvation of immediate teardown.
				 */
				DISABLE_FLAG (priv, IRIS_COORD_NEEDS_TEARDOWN | IRIS_COORD_EXCLUSIVE | IRIS_COORD_CONCURRENT);
				ENABLE_FLAG (priv, IRIS_COORD_TEARDOWN);
				resume = priv->teardown;
			}
			else
				g_assert_not_reached ();
		}
	}

	g_mutex_unlock (priv->mutex);

	if (resume) {
		iris_receiver_resume (resume);
	}
}

static void
iris_coordination_arbiter_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_coordination_arbiter_parent_class)->finalize (object);
}

static void
iris_coordination_arbiter_class_init (IrisCoordinationArbiterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	IrisArbiterClass *arbiter_class = IRIS_ARBITER_CLASS (klass);

	arbiter_class->can_receive = iris_coordination_arbiter_can_receive;
	arbiter_class->receive_completed = iris_coordination_arbiter_receive_completed;
	object_class->finalize = iris_coordination_arbiter_finalize;

	g_type_class_add_private (object_class, sizeof(IrisCoordinationArbiterPrivate));
}

static void
iris_coordination_arbiter_init (IrisCoordinationArbiter *arbiter)
{
	arbiter->priv = G_TYPE_INSTANCE_GET_PRIVATE (arbiter,
	                                             IRIS_TYPE_COORDINATION_ARBITER,
	                                             IrisCoordinationArbiterPrivate);
	arbiter->priv->mutex = g_mutex_new ();
	ENABLE_FLAG (arbiter->priv, IRIS_COORD_CONCURRENT);
}

IrisArbiter*
iris_coordination_arbiter_new (IrisReceiver *exclusive,
                               IrisReceiver *concurrent,
                               IrisReceiver *teardown)
{
	IrisCoordinationArbiter *arbiter;

	g_return_val_if_fail (exclusive  == NULL || IRIS_IS_RECEIVER (exclusive),  NULL);
	g_return_val_if_fail (concurrent == NULL || IRIS_IS_RECEIVER (concurrent), NULL);
	g_return_val_if_fail (teardown   == NULL || IRIS_IS_RECEIVER (teardown),   NULL);
	
	arbiter = g_object_new (IRIS_TYPE_COORDINATION_ARBITER, NULL);
	arbiter->priv->exclusive = exclusive ? g_object_ref (exclusive) : NULL;
	arbiter->priv->concurrent = concurrent ? g_object_ref (concurrent) : NULL;
	arbiter->priv->teardown = teardown ? g_object_ref (teardown) : NULL;

	ATTACH_ARBITER (exclusive, arbiter);
	ATTACH_ARBITER (concurrent, arbiter);
	ATTACH_ARBITER (teardown, arbiter);

	return IRIS_ARBITER (arbiter);
}
