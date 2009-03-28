#ifndef __MOCK_CALLBACK_RECEIVER_H__
#define __MOCK_CALLBACK_RECEIVER_H__

#include <glib-object.h>
#include <iris/iris-receiver.h>

G_BEGIN_DECLS

#define MOCK_TYPE_CALLBACK_RECEIVER				(mock_callback_receiver_get_type ())
#define MOCK_CALLBACK_RECEIVER(obj)				(G_TYPE_CHECK_INSTANCE_CAST ((obj), MOCK_TYPE_CALLBACK_RECEIVER, MockCallbackReceiver))
#define MOCK_CALLBACK_RECEIVER_CONST(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), MOCK_TYPE_CALLBACK_RECEIVER, MockCallbackReceiver const))
#define MOCK_CALLBACK_RECEIVER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), MOCK_TYPE_CALLBACK_RECEIVER, MockCallbackReceiverClass))
#define MOCK_IS_CALLBACK_RECEIVER(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MOCK_TYPE_CALLBACK_RECEIVER))
#define MOCK_IS_CALLBACK_RECEIVER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), MOCK_TYPE_CALLBACK_RECEIVER))
#define MOCK_CALLBACK_RECEIVER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), MOCK_TYPE_CALLBACK_RECEIVER, MockCallbackReceiverClass))

typedef struct _MockCallbackReceiver		MockCallbackReceiver;
typedef struct _MockCallbackReceiverClass	MockCallbackReceiverClass;
typedef struct _MockCallbackReceiverPrivate	MockCallbackReceiverPrivate;

struct _MockCallbackReceiver
{
	IrisReceiver parent;
	
	MockCallbackReceiverPrivate *priv;
};

struct _MockCallbackReceiverClass
{
	IrisReceiverClass parent_class;
};

GType         mock_callback_receiver_get_type (void) G_GNUC_CONST;
IrisReceiver *mock_callback_receiver_new      (GCallback callback, gpointer data);

G_END_DECLS

#endif /* __MOCK_CALLBACK_RECEIVER_H__ */
