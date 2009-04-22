/* iris-service.c
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
#include "iris-port.h"
#include "iris-receiver.h"
#include "iris-service.h"
#include "iris-service-private.h"

G_DEFINE_ABSTRACT_TYPE (IrisService, iris_service, G_TYPE_OBJECT);

static void
iris_service_exclusive_message_handler (IrisMessage *message,
                                        gpointer     user_data)
{
	g_return_if_fail (IRIS_IS_SERVICE (user_data));
	IRIS_SERVICE_GET_CLASS (user_data)->handle_exclusive (user_data, message);
}

static void
iris_service_concurrent_message_handler (IrisMessage *message,
                                         gpointer     user_data)
{
	g_return_if_fail (IRIS_IS_SERVICE (user_data));
	IRIS_SERVICE_GET_CLASS (user_data)->handle_concurrent (user_data, message);
}

static void
iris_service_teardown_message_handler (IrisMessage *message,
                                       gpointer     user_data)
{
	g_return_if_fail (IRIS_IS_SERVICE (user_data));
	IRIS_SERVICE_GET_CLASS (user_data)->handle_stop (user_data);
	IRIS_SERVICE (user_data)->priv->started = FALSE;
}

static void
iris_service_handle_start_real (IrisService *service)
{
	IrisServicePrivate *priv;

	g_return_if_fail (IRIS_IS_SERVICE (service));
	g_return_if_fail (!service->priv->started);

	priv = service->priv;

	priv->arbiter =
	iris_arbiter_coordinate (
		priv->exclusive_receiver =
		iris_arbiter_receive (
			priv->scheduler,
			priv->exclusive_port,
			iris_service_exclusive_message_handler,
			service),
		priv->concurrent_receiver =
		iris_arbiter_receive (
			priv->scheduler,
			priv->concurrent_port,
			iris_service_concurrent_message_handler,
			service),
		priv->teardown_receiver =
		iris_arbiter_receive (
			priv->scheduler,
			priv->teardown_port,
			iris_service_teardown_message_handler,
			service));

	g_assert (priv->arbiter);
	g_assert (priv->exclusive_receiver);
	g_assert (priv->concurrent_receiver);
	g_assert (priv->teardown_receiver);

	priv->started = TRUE;
}

static void
iris_service_handle_stop_real (IrisService *service)
{
	g_return_if_fail (IRIS_IS_SERVICE (service));
	service->priv->started = FALSE;
}

static IrisMessage*
iris_service_handle_stat_real (IrisService *service)
{
	IrisServicePrivate *priv;
	IrisMessage        *message;

	g_return_val_if_fail (IRIS_IS_SERVICE (service), NULL);

	priv = service->priv;

	message = iris_message_new_full (0,
			"Service::Started", G_TYPE_BOOLEAN, priv->started,
			NULL);

	return message;
}

static void
iris_service_handle_exclusive_real (IrisService *service,
                                    IrisMessage *message)
{
}

static void
iris_service_handle_concurrent_real (IrisService *service,
                                     IrisMessage *message)
{
}

static void
iris_service_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_service_parent_class)->finalize (object);
}

static void
iris_service_class_init (IrisServiceClass *service_class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (service_class);
	object_class->finalize = iris_service_finalize;

	service_class->handle_start = iris_service_handle_start_real;
	service_class->handle_stop = iris_service_handle_stop_real;
	service_class->handle_stat = iris_service_handle_stat_real;
	service_class->handle_exclusive = iris_service_handle_exclusive_real;
	service_class->handle_concurrent = iris_service_handle_concurrent_real;

	g_type_class_add_private (object_class, sizeof (IrisServicePrivate));
}

static void
iris_service_init (IrisService *service)
{
	service->priv = G_TYPE_INSTANCE_GET_PRIVATE (service,
	                                             IRIS_TYPE_SERVICE,
	                                             IrisServicePrivate);
	service->priv->exclusive_port  = iris_port_new ();
	service->priv->concurrent_port = iris_port_new ();
	service->priv->teardown_port   = iris_port_new ();
}

/**
 * iris_service_is_started:
 * @service: An #IrisService
 *
 * See iris_service_start().
 *
 * Return value: %TRUE if the service has been started
 */
gboolean
iris_service_is_started (IrisService *service)
{
	g_return_val_if_fail (IRIS_IS_SERVICE (service), FALSE);
	return service->priv->started;
}

/**
 * iris_service_start:
 * @service: An #IrisService
 *
 * Starts the #IrisService.
 */
void
iris_service_start (IrisService *service)
{
	IrisServicePrivate *priv;

	g_return_if_fail (IRIS_IS_SERVICE (service));

	priv = service->priv;

	if (priv->started)
		return;

	IRIS_SERVICE_GET_CLASS (service)->handle_start (service);
}

/**
 * iris_service_stop:
 * @service: An #IrisService
 *
 * Stops a running #IrisService
 */
void
iris_service_stop (IrisService *service)
{
	IrisServicePrivate *priv;
	IrisMessage        *message;

	g_return_if_fail (IRIS_IS_SERVICE (service));

	priv = service->priv;

	if (!priv->started)
		return;

	IRIS_SERVICE_GET_CLASS (service)->handle_stop (service);

	message = iris_message_new (0);
	iris_port_post (priv->teardown_port, message);
	iris_message_unref (message);
}

/**
 * iris_service_send_exclusive:
 * @service: An #IrisService
 * @message: An #IrisMessage
 *
 * Sends an exclusive message to the service. The message is guaranteed
 * to be handled while no other handlers are executing.
 */
void
iris_service_send_exclusive (IrisService *service,
                             IrisMessage *message)
{
	IrisServicePrivate *priv;

	g_return_if_fail (IRIS_IS_SERVICE (service));

	priv = service->priv;

	iris_port_post (priv->exclusive_port, message);
}

/**
 * iris_service_send_concurrent:
 * @service: An #IrisService
 * @message: An #IrisMessage
 *
 * Sends a concurrent message to the service. The message can be handled
 * concurrently with other concurrent messages to the service.
 */
void
iris_service_send_concurrent (IrisService *service,
                              IrisMessage *message)
{
	IrisServicePrivate *priv;

	g_return_if_fail (IRIS_IS_SERVICE (service));

	priv = service->priv;

	iris_port_post (priv->concurrent_port, message);
}
