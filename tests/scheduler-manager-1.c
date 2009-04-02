#include <iris/iris.h>
#include <string.h>

static GMainLoop *main_loop = NULL;

static gboolean
dummy (gpointer data)
{
	return FALSE;
}

static void
main_context1_cb (IrisMessage *message,
                  gpointer     data)
{
	gint *counter = data;
	g_atomic_int_inc (counter);
}

static void
main_context1 (void)
{
	IrisScheduler *scheduler;
	IrisReceiver  *receiver;
	IrisPort      *port;
	IrisMessage   *message;
	gint           counter = 0;
	gint           i;

	// Use basic scheduler
	scheduler = iris_scheduler_new ();
	g_assert (scheduler != NULL);
	receiver = iris_receiver_new_full (scheduler, NULL, main_context1_cb, &counter);
	g_assert (receiver != NULL);

	port = iris_port_new ();
	g_assert (port != NULL);
	iris_port_set_receiver (port, receiver);

	for (i = 0; i < 100; i++) {
		message = iris_message_new (1);
		iris_port_post (port, message);
	}

	// THIS IS A RACE CONDITION. But seems to be long enough for my
	// testing so far. Of course, its the entire reason we will be
	// making IrisTask soon.
	g_usleep (G_USEC_PER_SEC / 4);

	g_assert (counter == 100);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/scheduler-manager/main_context1", main_context1);

	return g_test_run ();
}
