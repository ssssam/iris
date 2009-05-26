/* iris-arbiter.h
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

#ifndef __IRIS_ARBITER_H__
#define __IRIS_ARBITER_H__

#include <glib-object.h>

#include "iris-types.h"
#include "iris-receiver.h"

G_BEGIN_DECLS

#define IRIS_TYPE_ARBITER (iris_arbiter_get_type ())

#define IRIS_ARBITER(obj)                  \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),    \
     IRIS_TYPE_ARBITER, IrisArbiter))

#define IRIS_ARBITER_CONST(obj)            \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),    \
     IRIS_TYPE_ARBITER, IrisArbiter const))

#define IRIS_ARBITER_CLASS(klass)          \
    (G_TYPE_CHECK_CLASS_CAST ((klass),     \
     IRIS_TYPE_ARBITER, IrisArbiterClass))

#define IRIS_IS_ARBITER(obj)               \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj),    \
     IRIS_TYPE_ARBITER))

#define IRIS_IS_ARBITER_CLASS(klass)       \
    (G_TYPE_CHECK_CLASS_TYPE ((klass),     \
     IRIS_TYPE_ARBITER))

#define IRIS_ARBITER_GET_CLASS(obj)        \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),     \
     IRIS_TYPE_ARBITER, IrisArbiterClass))

#define IRIS_TYPE_RECEIVE_DECISION (iris_receive_decision_get_type())

struct _IrisArbiter
{
	GObject parent;

	IrisArbiterPrivate *priv;
};

struct _IrisArbiterClass
{
	GObjectClass  parent_class;

	IrisReceiveDecision (*can_receive)       (IrisArbiter  *arbiter,
	                                          IrisReceiver *receiver);
	void                (*receive_completed) (IrisArbiter  *arbiter,
	                                          IrisReceiver *receiver);
};

GType               iris_receive_decision_get_type (void) G_GNUC_CONST;
GType               iris_arbiter_get_type          (void) G_GNUC_CONST;

IrisReceiveDecision iris_arbiter_can_receive       (IrisArbiter  *arbiter,
                                                    IrisReceiver *receiver);

void                iris_arbiter_receive_completed (IrisArbiter  *arbiter,
                                                    IrisReceiver *receiver);

IrisReceiver*       iris_arbiter_receive           (IrisScheduler      *scheduler,
                                                    IrisPort           *port,
                                                    IrisMessageHandler  handler,
                                                    gpointer            user_data,
                                                    GDestroyNotify      notify);

IrisArbiter*        iris_arbiter_coordinate        (IrisReceiver *exclusive,
                                                    IrisReceiver *concurrent,
                                                    IrisReceiver *teardown);

G_END_DECLS

#endif /* __IRIS_ARBITER_H__ */
