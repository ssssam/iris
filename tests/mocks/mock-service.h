#ifndef __MOCK_SERVICE_H__
#define __MOCK_SERVICE_H__

#include <glib-object.h>
#include <iris/iris.h>
#include "mock-scheduler.h"
#include <iris/iris-service-private.h>

G_BEGIN_DECLS

#define MOCK_TYPE_SERVICE		(mock_service_get_type ())
#define MOCK_SERVICE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), MOCK_TYPE_SERVICE, MockService))
#define MOCK_SERVICE_CONST(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), MOCK_TYPE_SERVICE, MockService const))
#define MOCK_SERVICE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), MOCK_TYPE_SERVICE, MockServiceClass))
#define MOCK_IS_SERVICE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MOCK_TYPE_SERVICE))
#define MOCK_IS_SERVICE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), MOCK_TYPE_SERVICE))
#define MOCK_SERVICE_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), MOCK_TYPE_SERVICE, MockServiceClass))

typedef struct _MockService		MockService;
typedef struct _MockServiceClass	MockServiceClass;
typedef struct _MockServicePrivate	MockServicePrivate;

struct _MockService {
	IrisService parent;
	
	MockServicePrivate *priv;
};

struct _MockServiceClass {
	IrisServiceClass parent_class;
};

GType mock_service_get_type (void) G_GNUC_CONST;
IrisService *mock_service_new (void);

G_END_DECLS

#endif /* __MOCK_SERVICE_H__ */

#define MOCK_SERVICE_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE((object), MOCK_TYPE_SERVICE, MockServicePrivate))

struct _MockServicePrivate
{
	gpointer dummy;
};

G_DEFINE_TYPE (MockService, mock_service, IRIS_TYPE_SERVICE);

static void
mock_service_finalize (GObject *object)
{
	G_OBJECT_CLASS (mock_service_parent_class)->finalize (object);
}

static void
mock_service_handle_exclusive (IrisService *service, IrisMessage *message)
{
	g_assert (message != NULL);
	g_assert (message->what == 1);

	GFunc func = iris_message_get_pointer (message, "func");
	g_assert (func);
	gpointer data = iris_message_get_pointer (message, "data");
	func (data, NULL);
}

static void
mock_service_handle_concurrent (IrisService *service, IrisMessage *message)
{
	g_assert (message != NULL);
	g_assert (message->what == 2);

	GFunc func = iris_message_get_pointer (message, "func");
	g_assert (func);
	gpointer data = iris_message_get_pointer (message, "data");
	func (data, NULL);
}


static void
mock_service_class_init (MockServiceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	IrisServiceClass *service_class = IRIS_SERVICE_CLASS (klass);

	service_class->handle_exclusive = mock_service_handle_exclusive;
	service_class->handle_concurrent = mock_service_handle_concurrent;
	
	object_class->finalize = mock_service_finalize;

	g_type_class_add_private (object_class, sizeof(MockServicePrivate));
}

static void
mock_service_init (MockService *self)
{
	self->priv = MOCK_SERVICE_GET_PRIVATE (self);
}

IrisService*
mock_service_new ()
{
	IrisService *service = g_object_new (MOCK_TYPE_SERVICE, NULL);
	service->priv->scheduler = mock_scheduler_new ();
	return service;
}

void
mock_service_send_exclusive (MockService *service, GCallback func, gpointer data)
{
	IrisMessage *message = iris_message_new (1);

	iris_message_set_pointer (message, "func", func);
	iris_message_set_pointer (message, "data", data);

	iris_service_send_exclusive (IRIS_SERVICE (service), message);
}

void
mock_service_send_concurrent (MockService *service, GCallback func, gpointer data)
{
	IrisMessage *message = iris_message_new (2);

	iris_message_set_pointer (message, "func", func);
	iris_message_set_pointer (message, "data", data);

	iris_service_send_concurrent (IRIS_SERVICE (service), message);
}
