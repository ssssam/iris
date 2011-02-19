/* process-2.c
 *
 * Copyright (C) 2009-11 Sam Thursfield <ssssam@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 
 * 02110-1301 USA
 */
 
/* process-2: nastier test for process. */
 
#include <stdlib.h>
#include <iris.h>

#include "iris/iris-process-private.h"

/* wait_func: hold up the process until a flag is set to continue; useful for
 *            testing progress UI and also for breaking the scheduler
 */
static void
wait_func (IrisProcess *process,
           IrisMessage *work_item,
           gpointer     user_data)
{
	gint *wait_state = user_data;

	if (g_atomic_int_get (wait_state)==0)
		g_atomic_int_set (wait_state, 1);
	else
		while (g_atomic_int_get (wait_state) != 2)
			g_usleep (100000);
}


/*static void
main_loop_iteration_times (gint times)
{
	gint i;

	for (i=0; i<times; i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (50);
		g_thread_yield ();
	}
}*/

static void
hospital_handler (IrisMessage *message,
                  gpointer     data)
{
	gint *ambulance_received = data;

	g_atomic_int_inc (ambulance_received);
}

/* blocking: work function blocks indefinitely, while important messages are
 *           sent that must be received.
 * 
 *  This test requires that in the process subsystem, message processing and
 *  work function execution must be to be separated, or wait_func() will block
 *  one of the scheduler threads thus preventing any of the 'ambulance'
 *  messages that get queued for delivery in the same thread from ever
 *  processing.
 *
 *  In real life usage, the 'cancel' message is the most obvious parallel.
 */
/* FIXME: any point in this test still existing? */
static void
blocking_1 ()
{
	IrisScheduler *scheduler;
	IrisProcess   *process;
	IrisMessage   *message;
	IrisPort      *port;
	IrisReceiver  *hospital;
	gint ii;
	gint wait_state,
	     ambulance_received;

	/* Force 2 threads or less, to ensure test fails */
	scheduler = iris_scheduler_new_full (1, 2);
	iris_set_default_work_scheduler (scheduler);

	/* Test is executed 100 times to further try to make it fail */
	for (ii=0; ii<10; ii++) {
		wait_state = 0;
		ambulance_received = 0;

		port = iris_port_new ();

		/* 'Hospital' must receive the ambulance messages at all costs */
		hospital = iris_arbiter_receive (iris_get_default_control_scheduler (),
		                                 port,
		                                 hospital_handler, &ambulance_received, NULL);

		iris_arbiter_coordinate (hospital, NULL, NULL);

		/* Meanwhile, this tasks will block one schedule threads */
		process = iris_process_new_with_func (wait_func, &wait_state, NULL);

		message = iris_message_new (2000);
		iris_process_enqueue (process, message);

		message = iris_message_new (2001);
		iris_process_enqueue (process, message);
		iris_process_no_more_work (process);
		iris_process_run (process);

		/* Ambulance message */
		message = iris_message_new (998);
		iris_port_post (port, message);
		message = iris_message_new (999);
		iris_port_post (port, message);

		/* If the process is running in the default scheduler, one of the
		 * messages will not be received because one of the scheduler threads
		 * will be blocking in work_func().
		 */
		while (g_atomic_int_get (&ambulance_received) != 2) {
			g_usleep (250000);
			g_thread_yield ();
		}

		g_atomic_int_set (&wait_state, 2);

		while (!iris_process_is_finished (process)) {
			g_thread_yield ();
		}

		g_object_unref (process);

		g_object_unref (hospital);
		g_object_unref (port);
	}
}


int main(int argc, char *argv[]) {
	g_thread_init (NULL);
	g_type_init ();
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/process 2/blocking 1", blocking_1);

	return g_test_run ();
}


