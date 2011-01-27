#include <stdlib.h>
#include <iris/iris.h>
#include <iris/iris-debug.h>

static IrisScheduler *scheduler = NULL;
static gint counter = 0;
static GMutex *mutex = NULL;
static GCond *cond = NULL;
static gboolean done = FALSE;

#define MSG_ID   (1)
#define ITER_MAX (1000)

static void
msg_handler2_cb (IrisMessage *message,
                 gpointer     data)
{
	g_atomic_int_inc (&counter);

	if (G_UNLIKELY (counter == (ITER_MAX * ITER_MAX))) {
		g_atomic_int_set (&done, TRUE);
		g_mutex_lock (mutex);
		g_cond_signal (cond);
		g_mutex_unlock (mutex);
	}
}

static void
msg_handler_cb (IrisMessage *message,
                gpointer     data)
{
	IrisPort     *port = iris_port_new ();
	IrisReceiver *recv = iris_arbiter_receive (scheduler, port, msg_handler2_cb, NULL, NULL);

	gint i;
	for (i = 0; i < ITER_MAX; i++) {
		IrisMessage *msg = iris_message_new (MSG_ID);
		iris_port_post (port, msg);
		iris_message_unref (msg);
	}

	g_object_unref (port);
	g_object_unref (recv);
}

static void
recursive (void)
{
	IrisPort     *port;
	IrisMessage  *msg;
	IrisReceiver *recv;

	/* For every message received, we generate ITER_MAX more messages
	 * causing recursive contention on the work queues.  This is
	 * solely to help us test this common problem.
	 */

	scheduler = iris_wsscheduler_new ();
	//scheduler = iris_lfscheduler_new ();
	//scheduler = iris_scheduler_new ();

	port = iris_port_new ();
	recv = iris_arbiter_receive (scheduler, port, msg_handler_cb, NULL, NULL);
	mutex = g_mutex_new ();
	cond = g_cond_new ();
	iris_port_set_receiver (port, recv);
	
	gint i;
	for (i = 0; i < ITER_MAX; i++) {
		msg = iris_message_new (MSG_ID);
		iris_port_post (port, msg);
		iris_message_unref (msg);
	}

	iris_debug_message (IRIS_DEBUG_THREAD, "Done pushing items");

	g_object_unref (port);
	g_object_unref (recv);

	if (!done) {
		iris_debug_message (IRIS_DEBUG_THREAD, "Waiting for items to complete");
		g_mutex_lock (mutex);
		g_cond_wait (cond, mutex);
		g_mutex_unlock (mutex);
		iris_debug_message (IRIS_DEBUG_THREAD, "Signal received, all done");
	}
	else {
		iris_debug_message (IRIS_DEBUG_THREAD, "Items completed before we could block!");
	}
}

gint
main (gint   argc,
      gchar *argv[])
{
	g_type_init ();
	g_thread_init (NULL);
	recursive ();
	return EXIT_SUCCESS;
}
