#ifndef __MOCK_SERVICE_H__
#define __MOCK_SERVICE_H__

#include <glib-object.h>
#include <iris/iris.h>

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
mock_service_class_init (MockServiceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
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
	return g_object_new (MOCK_TYPE_SERVICE, NULL);
}
