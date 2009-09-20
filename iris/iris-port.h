/* iris-port.h
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

#ifndef __IRIS_PORT_H__
#define __IRIS_PORT_H__

#include <glib-object.h>

#include "iris-message.h"
#include "iris-receiver.h"

G_BEGIN_DECLS

#define IRIS_TYPE_PORT (iris_port_get_type ())

#define IRIS_PORT(obj)                  \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
     IRIS_TYPE_PORT, IrisPort))

#define IRIS_PORT_CONST(obj)            \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
     IRIS_TYPE_PORT, IrisPort const))

#define IRIS_PORT_CLASS(klass)          \
    (G_TYPE_CHECK_CLASS_CAST ((klass),  \
     IRIS_TYPE_PORT, IrisPortClass))

#define IRIS_IS_PORT(obj)               \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
     IRIS_TYPE_PORT))

#define IRIS_IS_PORT_CLASS(klass)       \
    (G_TYPE_CHECK_CLASS_TYPE ((klass),  \
     IRIS_TYPE_PORT))

#define IRIS_PORT_GET_CLASS(obj)        \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),  \
     IRIS_TYPE_PORT, IrisPortClass))

typedef struct _IrisPort        IrisPort;
typedef struct _IrisPortClass   IrisPortClass;
typedef struct _IrisPortPrivate IrisPortPrivate;

struct _IrisPort
{
	GObject parent;

	/*< private >*/
	IrisPortPrivate *priv;
};

struct _IrisPortClass
{
	GObjectClass  parent_class;

	void          (*post)         (IrisPort *port, IrisMessage *message);
	void          (*repost)       (IrisPort *port, IrisMessage *message);

	IrisReceiver* (*get_receiver) (IrisPort *port);
	void          (*set_receiver) (IrisPort *port, IrisReceiver *receiver);
};

GType         iris_port_get_type        (void) G_GNUC_CONST;
IrisPort*     iris_port_new             (void);

void          iris_port_post            (IrisPort *port, IrisMessage *message);
void          iris_port_repost          (IrisPort *port, IrisMessage *message);
void          iris_port_flush           (IrisPort *port);
gboolean      iris_port_is_paused       (IrisPort *port);

gboolean      iris_port_has_receiver    (IrisPort *port);
IrisReceiver* iris_port_get_receiver    (IrisPort *port);
void          iris_port_set_receiver    (IrisPort *port, IrisReceiver *receiver);

guint         iris_port_get_queue_count (IrisPort *port);

G_END_DECLS

#endif /* __IRIS_PORT_H__ */
