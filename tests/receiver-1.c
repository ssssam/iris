#include <iris.h>
#include <string.h>

#include <iris/iris-arbiter-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-scheduler-private.h>

#include "mocks/mock-scheduler.h"

#define TEST_MESSAGE_DESTROY  11

static void
get_type1 (void)
{
	g_assert (IRIS_TYPE_RECEIVER != G_TYPE_INVALID);
}

static void
message_handler (IrisMessage *message,
                 gpointer     data)
{
	if (message->what == TEST_MESSAGE_DESTROY) {
		IrisReceiver *receiver;

		receiver = IRIS_RECEIVER (iris_message_get_object (message, "receiver"));
		iris_receiver_destroy (receiver, TRUE, NULL, FALSE);
	}
}

static void
new_full1 (void)
{
	IrisReceiver  *receiver;
	IrisScheduler *scheduler;
	IrisArbiter   *arbiter;

	scheduler = mock_scheduler_new ();
	receiver = iris_arbiter_receive (scheduler, iris_port_new (), message_handler, NULL, NULL);
	arbiter = iris_arbiter_coordinate (NULL, receiver, NULL);

	g_assert (IRIS_IS_SCHEDULER (scheduler));
	g_assert (IRIS_IS_ARBITER (arbiter));
	g_assert (IRIS_IS_RECEIVER (receiver));

	g_assert (iris_receiver_has_arbiter (receiver));
	g_assert (iris_receiver_has_scheduler (receiver));
}

static void
message_delivered1_cb (IrisMessage *message,
                       gpointer     data)
{
	gboolean *completed = data;
	g_assert (message != NULL);
	*completed = TRUE;
}

static void
message_delivered1 (void)
{
	IrisReceiver  *receiver;
	IrisScheduler *scheduler;
	IrisMessage   *msg;
	IrisPort      *port;
	gboolean       completed = FALSE;

	scheduler = mock_scheduler_new ();
	g_assert (IRIS_IS_SCHEDULER (scheduler));

	port = iris_port_new ();
	receiver = iris_arbiter_receive (scheduler, port, message_delivered1_cb, &completed, NULL);
	g_assert (receiver == iris_port_get_receiver (port));

	msg = iris_message_new (1);
	iris_port_post (port, msg);

	g_assert (completed);
}

static void
many_message_delivered1_cb (IrisMessage *message,
                            gpointer     data)
{
	gint *counter = data;
	g_atomic_int_inc (counter);
}

static void
many_message_delivered1 (void)
{
	IrisReceiver  *receiver;
	IrisScheduler *scheduler;
	IrisMessage   *msg;
	IrisPort      *port;
	gint           counter = 0;
	gint           i;

	scheduler = mock_scheduler_new ();
	g_assert (IRIS_IS_SCHEDULER (scheduler));

	port = iris_port_new ();
	receiver = iris_arbiter_receive (scheduler, port, many_message_delivered1_cb, &counter, NULL);
	g_assert (receiver == iris_port_get_receiver (port));

	for (i = 0; i < 100; i++) {
		msg = iris_message_new (1);
		iris_port_post (port, msg);
	}

	g_assert_cmpint (counter, ==, 100);
}

/*static void
set_scheduler1_cb (IrisMessage *msg,
                   gpointer     data)
{
}*/

static void
set_scheduler1 (void)
{
	IrisReceiver *r;
	IrisScheduler *s1, *s2;

	s1 = iris_scheduler_new ();
	s2 = iris_scheduler_new ();

	r = iris_arbiter_receive (s1, iris_port_new (), message_handler, NULL, NULL);
	g_assert (iris_receiver_get_scheduler (r) == s1);

	iris_receiver_set_scheduler (r, s2);
	g_assert (iris_receiver_get_scheduler (r) == s2);
}

static void
test_destroy (void)
{
	IrisReceiver  *receiver;
	IrisPort      *port;

	port = iris_port_new ();
	receiver = iris_arbiter_receive (NULL, port,
	                                 message_handler, NULL, NULL);
	iris_arbiter_coordinate (receiver, NULL, NULL);
	g_object_ref (receiver);

	iris_receiver_destroy (receiver, FALSE, NULL, FALSE);

	g_assert_cmpint (G_OBJECT(receiver)->ref_count, ==, 1);
	g_assert_cmpint (G_OBJECT(port)->ref_count, ==, 1);

	g_object_unref (receiver);
	g_object_unref (port);
}

static void
test_destroy_from_message (void)
{
	IrisScheduler *scheduler = mock_scheduler_new ();
	IrisReceiver  *receiver;
	IrisPort      *port;
	IrisMessage   *message;

	port = iris_port_new ();
	receiver = iris_arbiter_receive (scheduler, port,
	                                 message_handler, NULL, NULL);
	iris_arbiter_coordinate (receiver, NULL, NULL);
	g_object_ref (receiver);

	/* If the message doesn't get freed properly it will hold a ref on the
	 * receiver still
	 */
	message = iris_message_new (TEST_MESSAGE_DESTROY);
	iris_message_set_object (message, "receiver", G_OBJECT (receiver));

	iris_port_post (port, message);

	g_assert_cmpint (G_OBJECT(receiver)->ref_count, ==, 1);
	g_assert_cmpint (G_OBJECT(port)->ref_count, ==, 1);
	g_object_unref (receiver);
	g_object_unref (port);
	g_object_unref (scheduler);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/receiver/get_type1", get_type1);
	g_test_add_func ("/receiver/new_full1", new_full1);
	g_test_add_func ("/receiver/message_delivered1", message_delivered1);
	g_test_add_func ("/receiver/many_message_delivered1", many_message_delivered1);
	g_test_add_func ("/receiver/set_scheduler1", set_scheduler1);
	g_test_add_func ("/receiver/destroy()", test_destroy);
	g_test_add_func ("/receiver/destroy() from message", test_destroy_from_message);

	return g_test_run ();
}
