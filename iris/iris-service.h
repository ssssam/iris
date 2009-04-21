/* iris-service.h
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

#ifndef __IRIS_SERVICE_H__
#define __IRIS_SERVICE_H__

#include <glib-object.h>

#include "iris-message.h"
#include "iris-scheduler.h"

G_BEGIN_DECLS

#define IRIS_TYPE_SERVICE		(iris_service_get_type ())
#define IRIS_SERVICE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_SERVICE, IrisService))
#define IRIS_SERVICE_CONST(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_SERVICE, IrisService const))
#define IRIS_SERVICE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_SERVICE, IrisServiceClass))
#define IRIS_IS_SERVICE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_SERVICE))
#define IRIS_IS_SERVICE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_SERVICE))
#define IRIS_SERVICE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_SERVICE, IrisServiceClass))

typedef struct _IrisService		IrisService;
typedef struct _IrisServiceClass	IrisServiceClass;
typedef struct _IrisServicePrivate	IrisServicePrivate;

struct _IrisService
{
	GObject             parent;

	/*< private >*/
	IrisServicePrivate *priv;
};

struct _IrisServiceClass
{
	GObjectClass parent_class;

	void         (*handle_start)      (IrisService *service);
	void         (*handle_stop)       (IrisService *service);
	IrisMessage* (*handle_stat)       (IrisService *service);
	void         (*handle_exclusive)  (IrisService *service, IrisMessage *message);
	void         (*handle_concurrent) (IrisService *service, IrisMessage *message);
};

GType        iris_service_get_type        (void) G_GNUC_CONST;
void         iris_service_start           (IrisService *service);
void         iris_service_stop            (IrisService *service);
IrisMessage* iris_service_stat            (IrisService *service);
void         iris_service_send_exclusive  (IrisService *service, IrisMessage *message);
void         iris_service_send_concurrent (IrisService *service, IrisMessage *message);
void         iris_service_set_scheduler   (IrisService *service, IrisScheduler *scheduler);
gboolean     iris_service_is_started      (IrisService *service);

G_END_DECLS

#endif /* __IRIS_SERVICE_H__ */
