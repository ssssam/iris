#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gthread.h>
#include <iris/iris.h>

#define ITER_MAX 100000
#define MSG_DO_SMTHNG 1

static void
do_something (IrisMessage *message,
              gpointer     data)
{
}

static void
basic (void)
{
	IrisScheduler *scheduler;
	IrisReceiver  *receiver;
	IrisPort      *port;
	IrisMessage   *msg;
	gint           i;

	/* Create our scheduler. It defaults to 2 workers or N cores,
	 * whichever is larger.
	 */
	scheduler = iris_scheduler_new ();

	/* Create a receiver which turns messages into action items
	 * that can be executed by the scheduler.
	 */
	receiver = iris_receiver_new_full (scheduler, NULL, do_something, NULL);

	/* Create a port to deliver messages to */
	port = iris_port_new ();

	/* Attach the receiver to the port */
	iris_port_set_receiver (port, receiver);

	/* Add a bunch of work items */
	for (i = 0; i < ITER_MAX * 10; i++) {
		/* new message to pass something blah */
		msg = iris_message_new (MSG_DO_SMTHNG);

		/* post the message to the port for delivery */
		iris_port_post (port, msg);

		/* we are done with the message, we can unfref it */
		iris_message_unref (msg);
	}

	while (TRUE) {
		iris_scheduler_manager_print_stat ();
		sleep (1);
	}
}

gint
main (gint  argc,
      char *argv[])
{
	/* Initialize the type system */
	g_type_init ();

	/* Initialize gthread, our threading core */
	g_thread_init (NULL);

	/* Run the test */
	basic ();

	return EXIT_SUCCESS;
}
