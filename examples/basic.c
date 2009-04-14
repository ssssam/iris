#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gthread.h>
#include <iris/iris.h>

#define ITER_MAX 1000000
#define MSG_DO_SMTHNG 1

static GCond  *cond  = NULL;
static GMutex *mutex = NULL;
static gint    count = 0;

static void
do_something (IrisMessage *message,
              gpointer     data)
{
	g_atomic_int_inc (&count);

	if (count == ITER_MAX) {
		g_mutex_lock (mutex);
		g_cond_signal (cond);
		g_mutex_unlock (mutex);
	}
}

static void
basic (void)
{
	IrisScheduler *scheduler;
	IrisReceiver  *receiver;
	IrisPort      *port;
	IrisMessage   *msg;
	gint           i;


	/* You can use various schedulers based on the type of work you
	 * are doing.  In this very simple test, we are creating a million
	 * work items from outside of worker threads. This particular setup
	 * works the fastest with the lock-free scheduler.  Keep in mind this
	 * is a bad choice for non-server type setups since it spins a lot
	 * while waiting for work items.
	 *
	 * But on a quad core, its rougly 2x faster than the others.
	 */
	//scheduler = iris_scheduler_new ();
	//scheduler = iris_wsscheduler_new ();
	scheduler = iris_lfscheduler_new ();

	/* Create a receiver which turns messages into action items
	 * that can be executed by the scheduler.
	 */
	receiver = iris_receiver_new_full (scheduler, NULL, do_something, NULL);

	/* Create a port to deliver messages to */
	port = iris_port_new ();

	/* Attach the receiver to the port */
	iris_port_set_receiver (port, receiver);

	/* create mutex and cond to wait on for finished */
	mutex = g_mutex_new ();
	cond = g_cond_new ();

	/* Add a bunch of work items */
	for (i = 0; i < ITER_MAX; i++) {
		/* new message to pass something blah */
		msg = iris_message_new (MSG_DO_SMTHNG);

		/* post the message to the port for delivery */
		iris_port_post (port, msg);

		/* we are done with the message, we can unfref it */
		iris_message_unref (msg);
	}

	g_mutex_lock (mutex);
	g_cond_wait (cond, mutex);
	g_mutex_unlock (mutex);
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
