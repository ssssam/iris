#include <iris.h>
#include <string.h>

#include "iris/iris-process-private.h"

/* FIXME: maybe call this scheduler-process-1 .. ? Is it really needed? */

static void
slow_counter_callback (IrisProcess *process,
                       IrisMessage *work_item,
                       gpointer     user_data)
{
	gint *counter_address = iris_message_get_pointer (work_item, "counter");
	g_atomic_int_inc (counter_address);
	g_usleep (500);
}

/*static void
sleep_task_func (IrisTask *task,
                 gpointer  user_data)
{
	// 1.5 secs to provoke the scheduler into requesting more threads
	g_usleep (1500000);
}*/

static void
message_handler (IrisMessage *message,
                 gpointer     user_data)
{
	gint *counter_address = iris_message_get_pointer (message, "counter");
	g_atomic_int_inc (counter_address);
}


static void
counter_enqueue_times (IrisProcess *process,
                       gint        *p_counter,
                       gint         times)
{
	IrisMessage *work_item;
	gint         i;

	for (i=0; i<times; i++) {
		work_item = iris_message_new (0);
		iris_message_set_pointer (work_item, "counter", p_counter);
		iris_process_enqueue (process, work_item);
	}
}

/* live_process: general check that no work items are lost by schedulers, using a process */
static void
test_live_process (void)
{
	IrisScheduler *scheduler;
	IrisProcess   *process;
	IrisPort      *port;
	IrisReceiver  *receiver;
	IrisMessage   *message;
	gint           process_counter,
	               message_counter;
	gint           n_threads, j;

	for (n_threads=4; n_threads<=8; n_threads++) {
		scheduler = iris_scheduler_new_full (n_threads, n_threads);
		iris_scheduler_set_default (scheduler);

		process_counter = 0;
		message_counter = 0;

		port = iris_port_new ();
		receiver = iris_arbiter_receive (scheduler,
		                                 port,
		                                 &message_handler, NULL, NULL);

		/* Set off this process, which will cause the process to start a
		 * transient thread to help out with the work ...
		 */
		process = iris_process_new_with_func (slow_counter_callback,
		                                      NULL, NULL);
		counter_enqueue_times (process, &process_counter, 20);
		iris_process_no_more_work (process);
		iris_process_run (process);

		while (g_atomic_int_get (&process_counter) < 10) {
			g_usleep (50000);
		}

		/* By this point, the scheduler has probably opened and closed
		 * transient thread, now let's check messages are still getting
		 * through.
		 *
		 * The possible bug is that if the transient thread is not properly
		 * removed, some of the delivery work for this message will be queued
		 * into the dead thread and will never run.
		 */

		for (j=0; j<10; j++) {
			message = iris_message_new (0);
			iris_message_set_pointer (message, "counter", &message_counter);
			iris_port_post (port, message);
			iris_message_unref (message);
		}

		while (g_atomic_int_get (&process_counter) < 20 ||
		       g_atomic_int_get (&message_counter) < 10) {
			/*g_print ("%i / %i\n", g_atomic_int_get (&process_counter),
			                      g_atomic_int_get (&message_counter));*/
			g_usleep (50000);
		}

		g_object_unref (receiver);
		g_object_unref (port);
		g_object_unref (process);
		g_object_unref (scheduler);
	}
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/scheduler/live process", test_live_process);

	return g_test_run ();
}
