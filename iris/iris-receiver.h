/* iris-receiver.h
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

#ifndef __IRIS_RECEIVER_H__
#define __IRIS_RECEIVER_H__

#include <glib-object.h>

#include "iris-arbiter.h"
#include "iris-message.h"
#include "iris-scheduler.h"

G_BEGIN_DECLS

#define IRIS_TYPE_RECEIVER (iris_receiver_get_type ())

#define IRIS_RECEIVER(obj)                  \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),     \
     IRIS_TYPE_RECEIVER, IrisReceiver))

#define IRIS_RECEIVER_CONST(obj)            \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),     \
     IRIS_TYPE_RECEIVER, IrisReceiver const))

#define IRIS_RECEIVER_CLASS(klass)          \
    (G_TYPE_CHECK_CLASS_CAST ((klass),      \
     IRIS_TYPE_RECEIVER, IrisReceiverClass))

#define IRIS_IS_RECEIVER(obj)               \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj),     \
     IRIS_TYPE_RECEIVER))

#define IRIS_IS_RECEIVER_CLASS(klass)       \
    (G_TYPE_CHECK_CLASS_TYPE ((klass),      \
     IRIS_TYPE_RECEIVER))

#define IRIS_RECEIVER_GET_CLASS(obj)        \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),      \
     IRIS_TYPE_RECEIVER, IrisReceiverClass))

typedef enum
{
	IRIS_DELIVERY_ACCEPTED          = 1,
	IRIS_DELIVERY_ACCEPTED_PAUSE    = 2,
	IRIS_DELIVERY_ACCEPTED_REMOVE   = 3,
	IRIS_DELIVERY_REMOVE            = 4,
} IrisDeliveryStatus;

typedef struct _IrisReceiver        IrisReceiver;
typedef struct _IrisReceiverClass   IrisReceiverClass;
typedef struct _IrisReceiverPrivate IrisReceiverPrivate;

struct _IrisReceiver
{
	GObject parent;

	IrisReceiverPrivate *priv;
};

struct _IrisReceiverClass
{
	GObjectClass parent_class;

	IrisDeliveryStatus (*deliver) (IrisReceiver *receiver, IrisMessage *message);
};

GType              iris_receiver_get_type (void) G_GNUC_CONST;
IrisReceiver*      iris_receiver_new      (void);
IrisReceiver*      iris_receiver_new_full (IrisScheduler *scheduler, IrisArbiter *arbiter);
IrisDeliveryStatus iris_receiver_deliver  (IrisReceiver *receiver, IrisMessage *message);
gboolean           iris_receiver_has_arbiter (IrisReceiver *receiver);

G_END_DECLS

#endif /* __IRIS_RECEIVER_H__ */
