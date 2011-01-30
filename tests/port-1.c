#include <iris/iris.h>
#include <string.h>

/* A less hacky solution is to link mock source files into a libtestutils, and
 * then link that with each test.
 */
#include "mocks/mock-callback-receiver.h"
#include "mocks/mock-callback-receiver.c"

#define ITER_COUNT 1000000
#define SHORT_ITER_COUNT 100

static void
get_type1 (void)
{
	g_assert (IRIS_TYPE_PORT != G_TYPE_INVALID);
}

static void
has_receiver1_cb (IrisMessage *msg,
                  gpointer     data)
{
}

static void
has_receiver1 (void)
{
	IrisPort     *port;
	IrisReceiver *receiver;

	port = iris_port_new ();
	receiver = iris_arbiter_receive (NULL, port, has_receiver1_cb, NULL, NULL);
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

static void
queue1_cb (gpointer data)
{
	gint *counter = data;
	g_atomic_int_inc (counter);
}

static void
queue1 (void)
{
	IrisMessage  *msg;
	IrisPort     *port;
	IrisReceiver *receiver;
	gint          counter = 0;
	gint          i = 0;

	port = iris_port_new ();
	receiver = mock_callback_receiver_new (G_CALLBACK (queue1_cb), &counter);
	iris_port_set_receiver (port, receiver);
	mock_callback_receiver_block (MOCK_CALLBACK_RECEIVER (receiver));

	for (i = 0; i < SHORT_ITER_COUNT; i++) {
		msg = iris_message_new (1);
		iris_port_post (port, msg);
	}

	/* queue1_cb should get called once since we queue immediately
	 * after that. */
	g_assert_cmpint (counter, ==, 1);

	/* make sure the queue size + waiting in port is the same as
	 * we delivered. */
	g_assert_cmpint (iris_port_get_queue_count (port), ==, SHORT_ITER_COUNT);
}

static void
queue2 (void)
{
	IrisMessage  *msg;
	IrisPort     *port;
	IrisReceiver *receiver;
	gint          counter = 0;
	gint          i = 0;

	port = iris_port_new ();
	receiver = mock_callback_receiver_new (G_CALLBACK (queue1_cb), &counter);
	iris_port_set_receiver (port, receiver);
	mock_callback_receiver_pause (MOCK_CALLBACK_RECEIVER (receiver));

	for (i = 0; i < SHORT_ITER_COUNT; i++) {
		msg = iris_message_new (1);
		iris_port_post (port, msg);
	}

	/* queue1_cb should get called once since we queue immediately after that. */
	g_assert_cmpint (counter, ==, 1);

	/* receiver is in accept and pause, so total queued should be total-1 */
	g_assert_cmpint (iris_port_get_queue_count (port), ==, SHORT_ITER_COUNT - 1);
}

static void
queue3 (void)
{
	IrisMessage  *msg;
	IrisPort     *port;
	IrisReceiver *receiver;
	gint          counter = 0;
	gint          i = 0;

	port = iris_port_new ();
	receiver = mock_callback_receiver_new (G_CALLBACK (queue1_cb), &counter);
	iris_port_set_receiver (port, receiver);
	mock_callback_receiver_oneshot (MOCK_CALLBACK_RECEIVER (receiver));

	for (i = 0; i < SHORT_ITER_COUNT; i++) {
		msg = iris_message_new (1);
		iris_port_post (port, msg);
	}

	/* queue1_cb should get called once since we queue immediately after that. */
	g_assert_cmpint (counter, ==, 1);

	/* receiver is in accept and pause, so total queued should be total-1 */
	g_assert_cmpint (iris_port_get_queue_count (port), ==, SHORT_ITER_COUNT - 1);
}

static void
flush1_cb (gpointer data)
{
	gint *counter = data;
	g_atomic_int_inc (counter);
}

static void
flush1 (void)
{
	IrisMessage  *msg;
	IrisPort     *port;
	IrisReceiver *receiver;
	gint          counter = 0;
	gint          i;

	port = iris_port_new ();
	receiver = mock_callback_receiver_new (G_CALLBACK (flush1_cb), &counter);
	mock_callback_receiver_pause (MOCK_CALLBACK_RECEIVER (receiver));
	iris_port_set_receiver (port, receiver);

	g_assert (port != NULL);
	g_assert (receiver != NULL);

	for (i = 0; i < SHORT_ITER_COUNT; i++) {
		msg = iris_message_new (1);
		g_assert (msg != NULL);
		iris_port_post (port, msg);
	}

	/* the first item is delivered, the rest pause. */
	g_assert_cmpint (iris_port_get_queue_count (port), ==, SHORT_ITER_COUNT - 1);
	mock_callback_receiver_reset (MOCK_CALLBACK_RECEIVER (receiver));

	g_assert_cmpint (iris_port_get_queue_count (port), ==, SHORT_ITER_COUNT - 1);

	/* normally, we wouldnt need to flush this. but we do for our
	 * mock receiver since it is not doing it for us.
	 */
	iris_port_flush (port, NULL);

	g_assert_cmpint (iris_port_get_queue_count (port), ==, 0);
}

static void
flush2 (void)
{
	IrisMessage  *msg;
	IrisPort     *port;
	IrisReceiver *receiver;
	gint          counter = 0;
	gint          i;

	port = iris_port_new ();
	receiver = mock_callback_receiver_new (G_CALLBACK (flush1_cb), &counter);
	mock_callback_receiver_block (MOCK_CALLBACK_RECEIVER (receiver));
	iris_port_set_receiver (port, receiver);

	g_assert (port != NULL);
	g_assert (receiver != NULL);

	for (i = 0; i < SHORT_ITER_COUNT; i++) {
		msg = iris_message_new (1);
		g_assert (msg != NULL);
		iris_port_post (port, msg);
	}

	/* the first item is delivered, the rest pause. */
	g_assert_cmpint (iris_port_get_queue_count (port), ==, SHORT_ITER_COUNT);

	/* old receiver was removed, lets create a new one and attach it so
	 * that everything flushes into the new receiver.
	 */

	receiver = mock_callback_receiver_new (NULL, NULL);
	iris_port_set_receiver (port, receiver);

	/* All items should be flushed to new receiver. */
	g_assert_cmpint (iris_port_get_queue_count (port), ==, 0);
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
	g_test_add_func ("/port/queue1", queue1);
	g_test_add_func ("/port/queue2", queue2);
	g_test_add_func ("/port/queue3", queue3);
	g_test_add_func ("/port/flush1", flush1);
	g_test_add_func ("/port/flush2", flush2);

	return g_test_run ();
}
