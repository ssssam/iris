#include <iris/iris.h>
#include <string.h>

#include "mocks/mock-callback-receiver.h"
#include "mocks/mock-callback-receiver.c"

#define ITER_COUNT 1000000

static void
get_type1 (void)
{
	g_assert (IRIS_TYPE_PORT != G_TYPE_INVALID);
}

static void
has_receiver1 (void)
{
	IrisPort     *port;
	IrisReceiver *receiver;

	port = iris_port_new ();
	receiver = iris_receiver_new ();

	g_assert (iris_port_has_receiver (port) == FALSE);
	iris_port_set_receiver (port, receiver);
	g_assert (iris_port_has_receiver (port) == TRUE);
	iris_port_set_receiver (port, NULL);
	g_assert (iris_port_has_receiver (port) == FALSE);
}

static void
deliver1_cb (gpointer data)
{
	gboolean *success = data;
	g_return_if_fail (success != NULL);
	*success = TRUE;
}

static void
deliver1 (void)
{
	IrisPort     *port;
	IrisReceiver *receiver;
	IrisMessage  *message;
	gboolean      success = FALSE;

	port = iris_port_new ();
	receiver = mock_callback_receiver_new (G_CALLBACK (deliver1_cb), &success);
	iris_port_set_receiver (port, receiver);

	g_assert (success == FALSE);
	message = iris_message_new (0);
	iris_port_post (port, message);
	g_assert (success == TRUE);
}

static void
many_deliver1_cb (gpointer data)
{
	gint *counter = data;
	g_atomic_int_inc (counter);
}

static void
many_deliver1 (void)
{
	IrisPort     *port;
	IrisReceiver *receiver;
	IrisMessage  *msg;
	gint          counter = 0;
	gint          i;

	port = iris_port_new ();
	receiver = mock_callback_receiver_new (G_CALLBACK (many_deliver1_cb), &counter);
	iris_port_set_receiver (port, receiver);

	for (i = 0; i < ITER_COUNT; i++) {
		msg = iris_message_new (1);
		iris_port_post (port, msg);
	}

	g_assert_cmpint (counter, ==, ITER_COUNT);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/port/get_type1", get_type1);
	g_test_add_func ("/port/has_receiver1", has_receiver1);
	g_test_add_func ("/port/deliver1", deliver1);
	g_test_add_func ("/port/many_deliver1", many_deliver1);

	return g_test_run ();
}
