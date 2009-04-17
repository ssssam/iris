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
#include "iris-port.h"
#include "iris-receiver.h"
#include "iris-receiver-private.h"

#define ATTACH_ARBITER(r,a)                                  \
	G_STMT_START {                                       \
		if (r && !r->priv->arbiter)                  \
			r->priv->arbiter = g_object_ref (a); \
	} G_STMT_END

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

struct _IrisCoordinationArbiterPrivate
{
	IrisReceiver *exclusive;
	IrisReceiver *concurrent;
	IrisReceiver *teardown;
};

G_DEFINE_TYPE (IrisCoordinationArbiter,
               iris_coordination_arbiter,
               IRIS_TYPE_ARBITER);

static IrisReceiveDecision
iris_coordination_arbiter_can_receive (IrisArbiter  *arbiter,
                                      IrisReceiver *receiver)
{
	return IRIS_RECEIVE_NOW;
}

static void
iris_coordination_arbiter_receive_completed (IrisArbiter  *arbiter,
                                            IrisReceiver *receiver)
{
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
	arbiter->priv->exclusive = exclusive;
	arbiter->priv->concurrent = concurrent;
	arbiter->priv->teardown = teardown;

	ATTACH_ARBITER (exclusive, arbiter);
	ATTACH_ARBITER (concurrent, arbiter);
	ATTACH_ARBITER (teardown, arbiter);

	return IRIS_ARBITER (arbiter);
}
