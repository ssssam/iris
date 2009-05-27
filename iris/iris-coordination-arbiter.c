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
can_receive (IrisArbiter  *arbiter,
             IrisReceiver *receiver)
{
	IrisCoordinationArbiter        *coord;
	IrisCoordinationArbiterPrivate *priv;
	IrisReceiver                   *resume   = NULL;
	IrisReceiveDecision             decision = IRIS_RECEIVE_NEVER;

	g_return_val_if_fail (IRIS_IS_COORDINATION_ARBITER (arbiter), IRIS_RECEIVE_NEVER);
	g_return_val_if_fail (IRIS_IS_RECEIVER (receiver), IRIS_RECEIVE_NEVER);

	coord = IRIS_COORDINATION_ARBITER (arbiter);
	priv = coord->priv;

	g_static_rec_mutex_lock (&priv->mutex);

	/* Current Receiver: ANY
	 * Request Receiver: ANY
	 * Has Active......: ANY
	 * Pending.........: ANY
	 * Completed.......: YES
	 * Receive.........: NEVER
	 */
	if (priv->flags & IRIS_COORD_COMPLETE) {
		decision = IRIS_RECEIVE_NEVER;
		goto finish;
	}

	/* Current Receiver: TEARDOWN
	 * Request Receiver: CONCURRENT or EXCLUSIVE
	 * Has Active......: ANY
	 * Pending.........: ANY
	 * Receive.........: NEVER
	 */
	if (priv->flags & IRIS_COORD_TEARDOWN) {
		if (receiver == priv->concurrent || receiver == priv->exclusive) {
			decision = IRIS_RECEIVE_NEVER;
			goto finish;
		}
	}

	/* Current Receiver: TEARDOWN
	 * Request Receiver: TEARDOWN
	 * Has Active......: 0
	 * Pending.........: NONE
	 * Completed.......: NO
	 * Receive.........: NOW
	 */
	if (priv->flags & IRIS_COORD_TEARDOWN) {
		if ((priv->flags & IRIS_COORD_COMPLETE) == 0) {
			if (receiver == priv->teardown) {
				if (priv->active == 0) {
					decision = IRIS_RECEIVE_NOW;
					priv->flags &= ~IRIS_COORD_NEEDS_TEARDOWN;
					priv->flags |= IRIS_COORD_COMPLETE;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: TEARDOWN
	 * Request Receiver: TEARDOWN
	 * Has Active......: YES
	 * Pending.........: NONE
	 * Completed.......: NO
	 * Receive.........: NEVER
	 */
	if (priv->flags & IRIS_COORD_TEARDOWN) {
		if (receiver == priv->teardown) {
			if (priv->active > 0) {
				if ((priv->flags & IRIS_COORD_COMPLETE) == 0) {
					decision = IRIS_RECEIVE_NEVER;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: ANY
	 * Request Receiver: CONCURRENT or EXCUSIVE
	 * Has Active......: ANY
	 * Pending.........: TEARDOWN
	 * Receive.........: NEVER
	 */
	if (receiver == priv->concurrent || receiver == priv->exclusive) {
		if (priv->flags & IRIS_COORD_NEEDS_TEARDOWN) {
			decision = IRIS_RECEIVE_NEVER;
			goto finish;
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: CONCURRENT
	 * Has Active......: *
	 * Pending.........: NONE or CONCURRENT
	 * Receive.........: NOW
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->concurrent) {
			if (((priv->flags & IRIS_COORD_NEEDS_ANY) | IRIS_COORD_NEEDS_CONCURRENT) == IRIS_COORD_NEEDS_CONCURRENT) {
				decision = IRIS_RECEIVE_NOW;
				priv->flags &= ~IRIS_COORD_NEEDS_CONCURRENT;
				resume = priv->concurrent;
				goto finish;
			}
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: CONCURRENT
	 * Has Active......: *
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->concurrent) {
			if ((priv->flags & IRIS_COORD_NEEDS_ANY) != 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_CONCURRENT;
				goto finish;
			}
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: EXCLUSIVE
	 * Has Active......: YES
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->exclusive) {
			if (priv->active > 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_EXCLUSIVE;
				goto finish;
			}
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: EXCLUSIVE
	 * Has Active......: NO
	 * Pending.........: NONE or EXCLUSIVE
	 * Receive.........: NOW
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->exclusive) {
			if (priv->active == 0) {
				if (((priv->flags & IRIS_COORD_NEEDS_ANY) | IRIS_COORD_NEEDS_EXCLUSIVE) == IRIS_COORD_NEEDS_EXCLUSIVE) {
					decision = IRIS_RECEIVE_NOW;
					priv->flags &= ~(IRIS_COORD_CONCURRENT | IRIS_COORD_NEEDS_EXCLUSIVE);
					priv->flags |= IRIS_COORD_EXCLUSIVE;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: EXCLUSIVE
	 * Has Active......: NO
	 * Pending.........: EXCLUSIVE or TEARDOWN
	 * Receive.........: NOW
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->exclusive) {
			if (priv->active == 0) {
				if ((priv->flags & IRIS_COORD_NEEDS_ANY) != IRIS_COORD_NEEDS_CONCURRENT) {
					decision = IRIS_RECEIVE_NOW;
					priv->flags &= ~(IRIS_COORD_CONCURRENT | IRIS_COORD_NEEDS_EXCLUSIVE);
					priv->flags |= IRIS_COORD_EXCLUSIVE;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: TEARDOWN
	 * Has Active......: NO
	 * Pending.........: NONE or TEARDOWN
	 * Receive.........: NOW
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->teardown) {
			if (priv->active == 0) {
				if (((priv->flags & IRIS_COORD_NEEDS_ANY) | IRIS_COORD_NEEDS_TEARDOWN) == IRIS_COORD_NEEDS_TEARDOWN) {
					decision = IRIS_RECEIVE_NOW;
					priv->flags &= ~(IRIS_COORD_CONCURRENT | IRIS_COORD_NEEDS_TEARDOWN);
					priv->flags |= IRIS_COORD_TEARDOWN;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: TEARDOWN
	 * Has Active......: YES
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->teardown) {
			if (priv->active > 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_TEARDOWN;
				goto finish;
			}
		}
	}

	/* Current Receiver: CONCURRENT
	 * Request Receiver: TEARDOWN
	 * Has Active......: NO
	 * Pending.........: ANY
	 * Receive.........: NOW
	 */
	if ((priv->flags & IRIS_COORD_CONCURRENT) != 0) {
		if (receiver == priv->teardown) {
			if (priv->active == 0) {
				decision = IRIS_RECEIVE_NOW;
				priv->flags &= ~(IRIS_COORD_CONCURRENT | IRIS_COORD_NEEDS_TEARDOWN);
				priv->flags |= IRIS_COORD_TEARDOWN;
				goto finish;
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: EXCLUSIVE
	 * Has Active......: YES
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->exclusive) {
			if (priv->active > 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_EXCLUSIVE;
				goto finish;
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: EXCLUSIVE
	 * Has Active......: NO
	 * Pending.........: ANY
	 * Receive.........: NOW
	 * Notes...........: This should help us utilize our exclusive mode
	 *                   better so we don't do so many switches when
	 *                   already in exclusive mode.
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->exclusive) {
			if (priv->active == 0) {
				decision = IRIS_RECEIVE_NOW;
				priv->flags &= ~IRIS_COORD_NEEDS_EXCLUSIVE;
				goto finish;
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: EXCLUSIVE
	 * Has Active......: NO
	 * Pending.........: CONCURRENT or TEARDOWN
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->exclusive) {
			if (priv->active == 0) {
				if ((priv->flags & IRIS_COORD_NEEDS_ANY) & ~IRIS_COORD_NEEDS_EXCLUSIVE) {
					decision = IRIS_RECEIVE_LATER;
					priv->flags |= IRIS_COORD_NEEDS_EXCLUSIVE;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: EXCLUSIVE
	 * Has Active......: YES
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->exclusive) {
			if (priv->active > 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_EXCLUSIVE;
				goto finish;
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: CONCURRENT
	 * Has Active......: NO
	 * Pending.........: NONE or CONCURRENT
	 * Receive.........: NOW
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->concurrent) {
			if (priv->active == 0) {
				if (((priv->flags & IRIS_COORD_NEEDS_ANY) | IRIS_COORD_NEEDS_CONCURRENT) == IRIS_COORD_NEEDS_CONCURRENT) {
					decision = IRIS_RECEIVE_NOW;
					priv->flags &= ~(IRIS_COORD_EXCLUSIVE | IRIS_COORD_NEEDS_CONCURRENT);
					priv->flags |= IRIS_COORD_CONCURRENT;
					resume = priv->concurrent;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: CONCURRENT
	 * Has Active......: NO
	 * Pending.........: EXCLUSIVE or TEARDOWN
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->concurrent) {
			if (priv->active == 0) {
				if ((priv->flags & IRIS_COORD_NEEDS_ANY) & ~IRIS_COORD_NEEDS_CONCURRENT) {
					decision = IRIS_RECEIVE_LATER;
					priv->flags |= IRIS_COORD_NEEDS_CONCURRENT;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: CONCURRENT
	 * Has Active......: YES
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->concurrent) {
			if (priv->active > 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_CONCURRENT;
				goto finish;
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: TEARDOWN
	 * Has Active......: NO
	 * Pending.........: NONE or TEARDOWN
	 * Receive.........: NOW
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->teardown) {
			if (priv->active == 0) {
				if (((priv->flags & IRIS_COORD_NEEDS_ANY) | IRIS_COORD_NEEDS_TEARDOWN) == IRIS_COORD_NEEDS_TEARDOWN) {
					decision = IRIS_RECEIVE_NOW;
					priv->flags &= ~(IRIS_COORD_EXCLUSIVE | IRIS_COORD_NEEDS_TEARDOWN);
					priv->flags |= IRIS_COORD_TEARDOWN;
					goto finish;
				}
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: TEARDOWN
	 * Has Active......: YES
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->teardown) {
			if (priv->active > 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_TEARDOWN;
				goto finish;
			}
		}
	}

	/* Current Receiver: EXCLUSIVE
	 * Request Receiver: TEARDOWN
	 * Has Active......: NO
	 * Pending.........: ANY
	 * Receive.........: LATER
	 */
	if ((priv->flags & IRIS_COORD_EXCLUSIVE) != 0) {
		if (receiver == priv->teardown) {
			if (priv->active <= 0) {
				decision = IRIS_RECEIVE_LATER;
				priv->flags |= IRIS_COORD_NEEDS_TEARDOWN;
				goto finish;
			}
		}
	}

	g_print ("\nMISSING ARBITER BRANCH REPORT\n"
		 "====================================\n"
		 "Current.....: %s\n"
		 "Receiver....: %s\n"
		 "Active......: %lu\n"
		 "Pending.....: %u\n",
		 (priv->flags & IRIS_COORD_EXCLUSIVE) ? "EXCLUSIVE" : (priv->flags & IRIS_COORD_CONCURRENT) ? "CONCURRENT" : "TEARDOWN",
		 (receiver == priv->exclusive)        ? "EXCLUSIVE" : (receiver == priv->concurrent)        ? "CONCURRENT" : "TEARDOWN",
		 priv->active,
		 priv->flags & IRIS_COORD_NEEDS_ANY);

finish:
	if (decision == IRIS_RECEIVE_NOW) {
		if (receiver == priv->teardown)
			priv->flags |= IRIS_COORD_COMPLETE;
		g_atomic_int_inc ((gint*)&priv->active);
	}

	/* It would be nice to hold on to this lock while we resume to make
	 * sure our resuming receiver gets more in, but it can create a
	 * dead-lock if we are calling resume and try to lock on the receiver
	 * while someone in the receiver thread is trying to lock on us to
	 * try to deliver. */
	g_static_rec_mutex_unlock (&priv->mutex);

	if (resume)
		iris_receiver_resume (resume);

	return decision;
}

static void
receive_completed (IrisArbiter  *arbiter,
                   IrisReceiver *receiver)
{
	IrisCoordinationArbiter        *coord;
	IrisCoordinationArbiterPrivate *priv;
	IrisReceiver                   *resume = NULL;

	g_return_if_fail (IRIS_IS_COORDINATION_ARBITER (arbiter));
	g_return_if_fail (IRIS_IS_RECEIVER (receiver));

	coord = IRIS_COORDINATION_ARBITER (arbiter);
	priv = coord->priv;

	g_static_rec_mutex_lock (&priv->mutex);

	if (g_atomic_int_dec_and_test ((gint*)&priv->active)) {
		if (priv->flags & IRIS_COORD_COMPLETE) {
		}
		else if (priv->flags & IRIS_COORD_CONCURRENT) {
			if (priv->flags & IRIS_COORD_NEEDS_EXCLUSIVE) {
				priv->flags &= ~(IRIS_COORD_CONCURRENT | IRIS_COORD_NEEDS_EXCLUSIVE);
				priv->flags |= IRIS_COORD_EXCLUSIVE;
				resume = priv->exclusive;
			}
			else if (priv->flags & IRIS_COORD_NEEDS_TEARDOWN) {
				priv->flags &= ~(IRIS_COORD_CONCURRENT | IRIS_COORD_NEEDS_TEARDOWN);
				priv->flags |= IRIS_COORD_TEARDOWN;
				resume = priv->teardown;
			}
		}
		else if (priv->flags & IRIS_COORD_EXCLUSIVE) {
			if (priv->flags & IRIS_COORD_NEEDS_EXCLUSIVE) {
				/* Try to save mode switches by running exclusive now
				 * regardless of what other modes want to run. */
				resume = priv->exclusive;
			}
			else if (priv->flags & IRIS_COORD_NEEDS_CONCURRENT) {
				priv->flags &= ~(IRIS_COORD_EXCLUSIVE | IRIS_COORD_NEEDS_CONCURRENT);
				priv->flags |= IRIS_COORD_CONCURRENT;
				resume = priv->concurrent;
			}
			else if (priv->flags & IRIS_COORD_NEEDS_TEARDOWN) {
				priv->flags &= ~(IRIS_COORD_EXCLUSIVE | IRIS_COORD_NEEDS_TEARDOWN);
				priv->flags |= IRIS_COORD_TEARDOWN;
				resume = priv->teardown;
			}
		}
		else if (priv->flags & IRIS_COORD_TEARDOWN) {
			if ((priv->flags & IRIS_COORD_COMPLETE) == 0) {
				priv->flags &= ~IRIS_COORD_NEEDS_TEARDOWN;
				resume = priv->teardown;
			}
		}
		else {
			g_assert_not_reached ();
		}
	}

	g_static_rec_mutex_unlock (&priv->mutex);

	if (resume)
		iris_receiver_resume (resume);
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

	arbiter_class->can_receive = can_receive;
	arbiter_class->receive_completed = receive_completed;
	object_class->finalize = iris_coordination_arbiter_finalize;

	g_type_class_add_private (object_class, sizeof(IrisCoordinationArbiterPrivate));
}

static void
iris_coordination_arbiter_init (IrisCoordinationArbiter *arbiter)
{
	arbiter->priv = G_TYPE_INSTANCE_GET_PRIVATE (arbiter,
	                                             IRIS_TYPE_COORDINATION_ARBITER,
	                                             IrisCoordinationArbiterPrivate);
	g_static_rec_mutex_init (&arbiter->priv->mutex);
	arbiter->priv->flags = IRIS_COORD_CONCURRENT;
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

	g_return_val_if_fail (!exclusive  || exclusive  != concurrent, NULL);
	g_return_val_if_fail (!teardown   || exclusive  != teardown,   NULL);
	g_return_val_if_fail (!concurrent || concurrent != teardown,   NULL);
	g_return_val_if_fail (exclusive   || concurrent || teardown,   NULL);
	
	arbiter = g_object_new (IRIS_TYPE_COORDINATION_ARBITER, NULL);
	arbiter->priv->exclusive = exclusive ? g_object_ref (exclusive) : NULL;
	arbiter->priv->concurrent = concurrent ? g_object_ref (concurrent) : NULL;
	arbiter->priv->teardown = teardown ? g_object_ref (teardown) : NULL;

	ATTACH_ARBITER (exclusive, arbiter);
	ATTACH_ARBITER (concurrent, arbiter);
	ATTACH_ARBITER (teardown, arbiter);

	return IRIS_ARBITER (arbiter);
}
