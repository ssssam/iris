#include "mock-callback-receiver.h"

#define CALLBACK_RECEIVER_PRIVATE(object)  \
    (G_TYPE_INSTANCE_GET_PRIVATE((object), \
    MOCK_TYPE_CALLBACK_RECEIVER,           \
    MockCallbackReceiverPrivate))

struct _MockCallbackReceiverPrivate
{
	GCallback callback;
	gpointer  data;
};

G_DEFINE_TYPE (MockCallbackReceiver, mock_callback_receiver, IRIS_TYPE_RECEIVER);

static void
mock_callback_receiver_finalize (GObject *object)
{
	G_OBJECT_CLASS (mock_callback_receiver_parent_class)->finalize (object);
}

static IrisDeliveryStatus
deliver_impl (IrisReceiver *receiver,
              IrisMessage  *message)
{
	MockCallbackReceiver *cbr = MOCK_CALLBACK_RECEIVER (receiver);
	GSourceFunc callback = (GSourceFunc)cbr->priv->callback;

	if (callback)
		callback (cbr->priv->data);

	/* just discard the item */

	return IRIS_DELIVERY_ACCEPTED;
}

static void
mock_callback_receiver_class_init (MockCallbackReceiverClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	IrisReceiverClass *receiver_class = IRIS_RECEIVER_CLASS (klass);
	
	receiver_class->deliver = deliver_impl;
	object_class->finalize = mock_callback_receiver_finalize;

	g_type_class_add_private (object_class, sizeof (MockCallbackReceiverPrivate));
}

static void
mock_callback_receiver_init (MockCallbackReceiver *self)
{
	self->priv = CALLBACK_RECEIVER_PRIVATE (self);
}

IrisReceiver*
mock_callback_receiver_new (GCallback callback,
                            gpointer  data)
{
	MockCallbackReceiver *receiver = g_object_new (MOCK_TYPE_CALLBACK_RECEIVER, NULL);

	receiver->priv->callback = callback;
	receiver->priv->data = data;

	return IRIS_RECEIVER (receiver);
}
