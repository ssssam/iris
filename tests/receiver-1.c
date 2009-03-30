#include <iris/iris.h>
#include <string.h>

#include <iris/iris-arbiter-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-scheduler-private.h>

#include "mocks/mock-scheduler.h"

static void
get_type1 (void)
{
	g_assert (IRIS_TYPE_RECEIVER != G_TYPE_INVALID);
}

static void
new_full1 (void)
{
	IrisReceiver  *receiver;
	IrisScheduler *scheduler;
	IrisArbiter   *arbiter;

	scheduler = mock_scheduler_new ();
	arbiter = iris_arbiter_new ();
	receiver = iris_receiver_new_full (scheduler, arbiter, NULL, NULL);

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
	receiver = iris_receiver_new_full (scheduler,
	                                   NULL,
	                                   message_delivered1_cb,
	                                   &completed);

	g_assert (IRIS_IS_SCHEDULER (scheduler));
	g_assert (IRIS_IS_RECEIVER (receiver));

	port = iris_port_new ();
	iris_port_set_receiver (port, receiver);
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
	receiver = iris_receiver_new_full (scheduler,
	                                   NULL,
	                                   many_message_delivered1_cb,
	                                   &counter);

	g_assert (IRIS_IS_SCHEDULER (scheduler));
	g_assert (IRIS_IS_RECEIVER (receiver));

	port = iris_port_new ();
	iris_port_set_receiver (port, receiver);
	g_assert (receiver == iris_port_get_receiver (port));

	for (i = 0; i < 100; i++) {
		msg = iris_message_new (1);
		iris_port_post (port, msg);
	}

	g_assert_cmpint (counter, ==, 100);
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

	return g_test_run ();
}
