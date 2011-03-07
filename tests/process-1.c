/* process-1.c
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
 
/* process-1: tests for iris-process.c. */
 
#include <iris.h>

#include "iris/iris-process-private.h"
#include "iris/iris-receiver-private.h"

#include "g-repeated-test.h"
#include "mocks/mock-scheduler.h"

static void
wait_control_messages (IrisProcess *process)
{
	IrisPort *control_port;

	control_port = IRIS_TASK(process)->priv->port;

	while (iris_port_get_queue_length (control_port) > 0 ||
	       g_atomic_int_get (&iris_port_get_receiver (control_port)->priv->active) > 0)
		g_thread_yield ();
}

static void
dummy_func (IrisProcess *process,
            IrisMessage *work_item,
            gpointer     user_data)
{
}

static void
push_next_func (IrisProcess *process,
                IrisMessage *work_item,
                gpointer     user_data)
{
	gint delay = GPOINTER_TO_INT (user_data);

	iris_process_forward (process, work_item);

	if (delay)
		g_usleep (delay);
}

static void
wait_process_func (IrisProcess *process,
                   IrisMessage *work_item,
                   gpointer     user_data)
{
	gint *p_wait_state = user_data;

	g_assert_cmpint (g_atomic_int_get (p_wait_state), ==, 0);

	g_atomic_int_set (p_wait_state, 1);

	while (g_atomic_int_get (p_wait_state) != 2)
		g_thread_yield ();
}

/* Work item to increment a global counter, so we can tell if the whole queue
 * gets executed propertly
 */
static void
counter_callback (IrisProcess *process,
                  IrisMessage *work_item,
                  gpointer     user_data)
{
	gint *counter_address = iris_message_get_pointer (work_item, "counter");
	g_atomic_int_inc (counter_address);
}

/* Work item that must be run before counter_callback() */
static void
pre_counter_callback (IrisProcess *process,
                      IrisMessage *work_item,
                      gpointer     user_data)
{
	IrisMessage *new_work_item;
	const GValue *data = iris_message_get_data (work_item);

	new_work_item = iris_message_new (0);
	iris_message_set_pointer (new_work_item, "counter", g_value_get_pointer (data));

	iris_process_forward (process, new_work_item);
}

static void
recursive_counter_callback (IrisProcess *process,
                            IrisMessage *work_item,
                            gpointer     user_data)
{
	gint *counter_address = iris_message_get_pointer (work_item, "counter"),
	      i;

	if (*counter_address < 50) {
		for (i=0; i<2; i++) {
			IrisMessage *new_work_item = iris_message_new (0);
			iris_message_set_pointer (new_work_item, "counter", counter_address);
			iris_process_recurse (process, new_work_item);
		}
	}

	(*counter_address) ++;
}

static void
time_waster_callback (IrisProcess *process,
                      IrisMessage *work_item,
                      gpointer     user_data)
{
	if (iris_process_was_canceled (process))
		return;

	g_usleep (100000);
}

static void
enqueue_counter_work (IrisProcess   *process,
                      volatile gint *p_counter,
                      gint           times)
{
	gint i;

	for (i=0; i < times; i++) {
		IrisMessage *work_item = iris_message_new (0);
		iris_message_set_pointer (work_item, "counter", (gpointer)p_counter);
		iris_process_enqueue (process, work_item);
	}
	iris_process_no_more_work (process);
}


static void
test_lifecycle (void)
{
	IrisProcess *process;

	process = iris_process_new_with_func (NULL, NULL, NULL);
	g_object_add_weak_pointer (G_OBJECT (process), (gpointer *)&process);

	g_assert_cmpint (G_OBJECT (process)->ref_count, ==, 1);

	iris_process_no_more_work (process);
	iris_process_run (process);

	while (process != NULL)
		g_thread_yield ();
}

static void
test_simple (void)
{
	gint counter = 0;
	IrisProcess *process;

	process = iris_process_new_with_func (counter_callback, NULL, NULL);
	/*iris_task_set_control_scheduler (IRIS_TASK (process), mock_scheduler_new ());
	iris_task_set_work_scheduler (IRIS_TASK (process), mock_scheduler_new ());*/

	g_assert_cmpint (G_OBJECT (process)->ref_count, ==, 1);
	g_object_ref (process);

	iris_process_run (process);
	enqueue_counter_work (process, &counter, 50);

	while (! iris_process_is_finished (process))
		g_thread_yield ();

	g_assert_cmpint (counter, ==, 50);

	g_assert (iris_process_is_finished (process) == TRUE);
	g_assert (iris_process_was_canceled (process) == FALSE);
	g_assert (iris_process_has_succeeded (process) == TRUE);

	g_object_unref (process);
}

static void
test_cancel_creation (void)
{
	IrisProcess *process;

	process = iris_process_new_with_func (counter_callback, NULL, NULL);
	g_object_ref (process);

	iris_process_no_more_work (process);
	iris_process_cancel (process);

	while (! iris_process_is_finished (process)) {
		wait_control_messages (process);
		g_thread_yield ();
	}

	/* Should be only one ref, but we have no real way of testing if
	 * iris_task_finish() has fully executed so we must allow for the
	 * execution ref still being present ...
	 */
	g_assert_cmpint (G_OBJECT (process)->ref_count, <=, 2);

	g_assert (iris_process_was_canceled (process) == TRUE);
	g_assert (iris_process_has_succeeded (process) == FALSE);

	g_object_unref (process);
}

static void
test_cancel_preparation (void)
{
	gint         counter;
	IrisProcess *process;

	process = iris_process_new_with_func (counter_callback, NULL, NULL);
	g_object_ref (process);

	iris_process_cancel (process);
	wait_control_messages (process);

	g_assert (iris_process_was_canceled (process) == TRUE);
	g_assert (iris_process_is_finished (process) == FALSE);

	/* Enqueuing work is still allowed, process will not be freed until
	 * iris_process_no_more_work() is called
	 */
	counter = 0;
	enqueue_counter_work (process, &counter, 50);

	while (! iris_process_is_finished (process)) {
		wait_control_messages (process);
		g_thread_yield ();
	}

	g_assert_cmpint (G_OBJECT (process)->ref_count, <=, 2);
	g_assert (iris_process_was_canceled (process) == TRUE);
	g_assert (iris_process_has_succeeded (process) == FALSE);

	/* You can even still run the process, if it's not been destroyed,
	 * which should do nothing
	 */
	iris_process_run (process);
	wait_control_messages (process);

	g_assert_cmpint (G_OBJECT (process)->ref_count, ==, 1);
	g_assert (iris_process_was_canceled (process) == TRUE);
	g_assert (iris_process_is_finished (process) == TRUE);
	g_assert (iris_process_has_succeeded (process) == FALSE);

	g_object_unref (process);
}

static void
cancel_execution_cb (IrisProcess *progress,
                     IrisMessage *work_item,
                     gpointer     user_data) {
	volatile gint *p_wait_state = user_data;

	if (g_atomic_int_get (p_wait_state) == 0)
		*p_wait_state = 1;
	else
		while (g_atomic_int_get (p_wait_state) != 2)
			g_thread_yield ();
}

/* cancel in execution 1: Test for cancel on a normal run where
 * iris_process_no_more_work() was called before iris_process_run()
 */
static void
test_cancel_execution_1 (void)
{
	volatile gint  counter,
	               wait_state = 0;
	IrisProcess   *process;

	process = iris_process_new_with_func (cancel_execution_cb, (gpointer)&wait_state, NULL);
	g_object_ref (process);

	counter = 0;
	enqueue_counter_work (process, &counter, 50);

	iris_process_run (process);
	while (g_atomic_int_get (&wait_state) != 1)
		g_thread_yield();

	iris_process_cancel (process);
	while (! iris_process_was_canceled (process))
		wait_control_messages (process);

	/* Still in process work function at this point */
	g_assert (iris_process_is_executing (process) == TRUE);
	g_assert (iris_process_is_finished (process) == FALSE);

	g_atomic_int_set (&wait_state, 2);

	while (! iris_process_is_finished (process))
		wait_control_messages (process);

	g_assert_cmpint (G_OBJECT (process)->ref_count, <=, 2);
	g_assert (iris_process_is_executing (process) == FALSE);
	g_assert (iris_process_was_canceled (process) == TRUE);
	g_assert (iris_process_has_succeeded (process) == FALSE);

	g_object_unref (process);
}

/* cancel in execution 2: call iris_cancel() on a running process before
 * iris_process_no_more_work() has been called
 */
static void
test_cancel_execution_2 (void)
{
	volatile gint  counter;
	IrisProcess   *process;

	process = iris_process_new_with_func (counter_callback, NULL, NULL);
	g_object_ref (process);

	iris_process_run (process);
	while (! iris_process_is_executing (process))
		wait_control_messages (process);

	iris_process_cancel (process);
	while (! iris_process_was_canceled (process))
		wait_control_messages (process);

	/* The result of 'is_executing' is undefined in practice, because the
	 * cancel could have prevented the task from ever running. So we don't
	 * test for it.
	 */
	g_assert (iris_process_is_finished (process) == FALSE);

	counter = 0;
	enqueue_counter_work (process, &counter, 50);
	while (! iris_process_is_finished (process))
		wait_control_messages (process);

	g_assert (iris_process_was_canceled (process) == TRUE);
	g_assert (iris_process_has_succeeded (process) == FALSE);

	g_assert_cmpint (G_OBJECT (process)->ref_count, <=, 2);

	g_object_unref (process);
}

/* cancel in execution 3: Test work items are freed on cancel */
static void
test_cancel_execution_3 (void) {
	IrisProcess *process;
	IrisMessage *message[50];
	int          i;

	process = iris_process_new_with_func (time_waster_callback, NULL, NULL);
	g_object_ref (process);

	for (i=0; i<50; i++) {
		message[i] = iris_message_new (1);
		iris_message_ref (message[i]);
		iris_process_enqueue (process, message[i]);
	}
	iris_process_no_more_work (process);

	iris_process_run (process);
	while (! iris_process_is_executing (process))
		wait_control_messages (process);

	iris_process_cancel (process);
	while (! iris_process_is_finished (process))
		wait_control_messages (process);

	g_assert (iris_process_is_finished (process));

	g_assert_cmpint (G_OBJECT (process)->ref_count, <=, 2);
	g_object_unref (process);

	/* Queued work items should all have been freed */
	for (i=0; i<50; i++) {
		g_assert_cmpint (message[i]->ref_count, ==, 1);
		iris_message_unref (message[i]);
	}
};

/* titles: Check the title property does not break */
static void
titles (void)
{
	IrisProcess *process;;
	gint  counter = 0,
	      i;
	char *title;

	process = iris_process_new_with_func (counter_callback, NULL, NULL);
	g_object_add_weak_pointer (G_OBJECT (process), (gpointer *)&process);
	iris_process_set_title (process, "Title 1");
	iris_process_run (process);

	g_object_get (G_OBJECT (process), "title", &title, NULL);
	g_assert_cmpstr (title, ==, "Title 1");
	g_free (title);

	g_object_set (G_OBJECT (process), "title", NULL, NULL);

	for (i=0; i < 50; i++) {
		IrisMessage *work_item = iris_message_new (0);
		iris_message_set_pointer (work_item, "counter", &counter);
		iris_process_enqueue (process, work_item);
	}

	title = (char *)iris_process_get_title (process);
	g_assert_cmpstr (title, ==, NULL);

	iris_process_no_more_work (process);

	while (process != NULL)
		g_thread_yield ();

	g_assert_cmpint (counter, ==, 50);
}


static void
recurse_1 (void)
{
	IrisProcess *recursive_process;
	int counter = 0;

	recursive_process = iris_process_new_with_func
	                      (recursive_counter_callback, NULL, NULL);
	g_object_add_weak_pointer (G_OBJECT (recursive_process),
	                           (gpointer *)&recursive_process);

	iris_process_run (recursive_process);

	IrisMessage *work_item = iris_message_new (0);
	iris_message_set_pointer (work_item, "counter", &counter);
	iris_process_enqueue (recursive_process, work_item);
	iris_process_no_more_work (recursive_process);

	while (recursive_process != NULL)
		g_thread_yield ();

	g_assert_cmpint (counter, ==, 101);
}

/* chaining 1: basic test of source/sink connections */
static void
chaining_1 (void)
{
	IrisProcess *head_process = iris_process_new_with_func
	                              (push_next_func, NULL, NULL);
	IrisProcess *tail_process = iris_process_new_with_func
	                              (dummy_func, NULL, NULL);
	g_object_add_weak_pointer (G_OBJECT (head_process),
	                           (gpointer *)&head_process);
	g_object_add_weak_pointer (G_OBJECT (tail_process),
	                           (gpointer *)&tail_process);

	iris_process_connect (head_process, tail_process);

	iris_process_run (head_process);

	while (! iris_process_is_executing (head_process))
		wait_control_messages (head_process);
	wait_control_messages (tail_process);

	g_assert (iris_process_has_successor (head_process) == TRUE);
	g_assert (iris_process_has_predecessor (head_process) == FALSE);

	g_assert (iris_process_has_successor (tail_process) == FALSE);
	g_assert (iris_process_has_predecessor (tail_process) == TRUE);

	/* Chain will finish now */
	iris_process_no_more_work (head_process);

	while (head_process != NULL || tail_process != NULL)
		g_thread_yield ();
}

/* chaining 2: test chained work is actually executed */
static void
chaining_2 (void)
{
	IrisMessage *message[50];
	int          counter = 0,
	             i;
	
	IrisProcess *head_process = iris_process_new_with_func
	                              (pre_counter_callback, NULL, NULL);
	IrisProcess *tail_process = iris_process_new_with_func
	                              (counter_callback, NULL, NULL);
	g_object_ref (tail_process);

	iris_process_connect (head_process, tail_process);

	iris_process_run (head_process);

	for (i=0; i < 50; i++) {
		/* Set pointer as data instead of "counter" property, to ensure
		 * pre_counter_callback () is called to change it. */
		message[i] = iris_message_new_data (0, G_TYPE_POINTER, &counter);
		iris_message_ref (message[i]);
		iris_process_enqueue (head_process, message[i]);
	}
	iris_process_no_more_work (head_process);

	while (! iris_process_is_finished (tail_process))
		wait_control_messages (tail_process);

	g_assert_cmpint (counter, ==, 50);

	for (i=0; i < 50; i++) {
		/* Check that the work items all got unreffed correctly */
		g_assert_cmpint (message[i]->ref_count, ==, 1);
		iris_message_unref (message[i]);
	}

	g_object_unref (tail_process);
}

/* chaining 3: a lot of processes. */
static void
test_chaining_3 (void) {
	IrisProcess *process[5];
	IrisMessage *message;
	int          i;
	volatile int counter = 0;

	for (i=0; i<5; i++) {
		process[i] = iris_process_new_with_func (push_next_func, NULL, NULL);
		if (i > 0)
			iris_process_connect (process[i-1], process[i]);
	}
	iris_process_set_func (process[4], counter_callback, NULL, NULL);

	iris_process_run (process[0]);

	for (i=0; i < 50; i++) {
		message = iris_message_new_items
		            (1, "counter", G_TYPE_POINTER, &counter, NULL);
		iris_process_enqueue (process[0], message);
	}
	iris_process_no_more_work (process[0]);

	while (g_atomic_int_get (&counter) < 50)
		g_thread_yield ();
};

/* Cancel a chain and make sure they all exit */
static void
test_cancelling_chained (gconstpointer user_data) {
	IrisProcess *head_process, *mid_process, *tail_process;
	IrisMessage *message[50];
	gboolean     cancel_tail = GPOINTER_TO_INT (user_data);
	int i;

	head_process = iris_process_new_with_func (push_next_func, NULL, NULL);
	mid_process  = iris_process_new_with_func (push_next_func, NULL, NULL);
	tail_process = iris_process_new_with_func (time_waster_callback, NULL, NULL);
	g_object_ref (head_process);
	g_object_ref (tail_process);

	iris_process_connect (head_process, mid_process);
	iris_process_connect (mid_process, tail_process);

	iris_process_run (head_process);

	for (i=0; i < 50; i++) {
		message[i] = iris_message_ref (iris_message_new (i));
		iris_process_enqueue (head_process, message[i]);
	}
	iris_process_no_more_work (head_process);

	/* Deliberately don't wait for the connect messages to be processed before
	 * we send the cancel.
	 */

	if (cancel_tail) {
		iris_process_cancel (tail_process);
		while (! iris_process_is_finished (tail_process))
			wait_control_messages (head_process);
	} else {
		iris_process_cancel (head_process);
		while (! iris_process_is_finished (tail_process))
			wait_control_messages (tail_process);
	}

	g_object_unref (head_process);
	g_object_unref (tail_process);

	/* Check no work items were leaked */
	for (i=0; i < 50; i++) {
		g_assert_cmpint (message[i]->ref_count, ==, 1);
		iris_message_unref (message[i]);
	}
};

/* cancel before chained: cancel a process before chaining it to another.
 * This isn't encouraged but can happen in normal cases due to message delays.
 */
static void
test_cancel_before_chained (void) {
	IrisProcess *head_process, *tail_process;

	head_process = iris_process_new_with_func (time_waster_callback, NULL,
	                                           NULL);
	tail_process = iris_process_new_with_func (time_waster_callback, NULL,
	                                           NULL);
	g_object_ref (tail_process);

	iris_process_cancel (head_process);
	iris_process_connect (head_process, tail_process);

	iris_process_run (head_process);
	iris_process_no_more_work (head_process);

	while (! iris_process_is_finished (tail_process))
		wait_control_messages (tail_process);

	g_assert (iris_process_was_canceled (tail_process));

	g_object_unref (tail_process);
};

/* canceled chain is_finished(): the tail process should not return TRUE for
 * is_finished() until the whole chain has finished, even if the tail process
 * was explicitly canceled first.
 */
static void
test_canceled_chain_is_finished (void) {
	IrisProcess   *head_process, *tail_process;
	volatile gint  wait_state;

	head_process = iris_process_new_with_func (wait_process_func,
	                                           (gpointer)&wait_state, NULL);
	tail_process = iris_process_new_with_func (time_waster_callback,
	                                           NULL, NULL);
	iris_process_connect (head_process, tail_process);

	g_object_ref (tail_process);

	wait_state = 0;
	iris_process_enqueue (head_process, iris_message_new(0));
	iris_process_no_more_work (head_process);
	iris_process_run (head_process);

	while (g_atomic_int_get (&wait_state) != 1)
		g_thread_yield ();

	/* Head process is blocking now */
	iris_process_cancel (tail_process);

	while (! iris_process_was_canceled (tail_process))
		wait_control_messages (tail_process);

	/* Tail process is canceled, but the CHAIN cannot finish until head_process
	 * fully cancels so this should not return TRUE yet.
	 */
	g_assert (! iris_process_is_finished (tail_process));

	g_atomic_int_set (&wait_state, 2);

	/* Head process can now complete, so the chain should finish */
	while (! iris_process_is_finished (tail_process))
		wait_control_messages (tail_process);

	g_object_unref (tail_process);
};

/* Check output estimates are passed down */
static void
test_output_estimates_basic (void)
{
	IrisProcess *process[3];
	gint         i, j,
	             processed_items, total_items;

	for (i=0; i<3; i++) {
		process[i] = iris_process_new_with_func (time_waster_callback, NULL, NULL);
		g_object_ref (process[i]);
		if (i > 0)
			iris_process_connect (process[i-1], process[i]);
	}

	for (j=0; j<100; j++)
		iris_process_enqueue (process[0], iris_message_new (0));
	iris_process_no_more_work (process[0]);
	iris_process_run (process[0]);

	do
		iris_process_get_status (process[0], &processed_items, &total_items);
	while (total_items != 100);

	/* Empty message loop to ensure CHAIN_ESTIMATE messages get through */
	for (i=0; i<3; i++)
		wait_control_messages (process[i]);

	iris_process_get_status (process[1], &processed_items, &total_items);
	g_assert_cmpint (total_items, ==, 100);

	iris_process_get_status (process[2], &processed_items, &total_items);
	g_assert_cmpint (total_items, ==, 100);

	iris_process_cancel (process[0]);

	for (i=0; i<3; i++) {
		while (!iris_process_is_finished (process[i]))
			wait_control_messages (process[i]);
		g_object_unref (process[i]);
	}
}

static void
test_output_estimates_low (void)
{
	IrisProcess *process_1, *process_2;
	gint         j, counter,
	             processed_items, total_items;

	process_1 = iris_process_new_with_func (pre_counter_callback, NULL, NULL);
	process_2 = iris_process_new_with_func (counter_callback, NULL, NULL);
	iris_process_connect (process_1, process_2);
	g_object_ref (process_2);

	iris_process_set_output_estimation (process_1, 0.01);

	counter = 0;
	for (j=0; j<100; j++)
		iris_process_enqueue (process_1,
		                      iris_message_new_data (0, G_TYPE_POINTER, &counter));
	iris_process_run (process_1);
	iris_process_no_more_work (process_1);

	do
		iris_process_get_status (process_1, &processed_items, &total_items);
	while (total_items != 100);

	while (!iris_process_is_finished (process_2)) {
		iris_process_get_status (process_2, &processed_items, &total_items);
		g_assert_cmpint (processed_items, <=, total_items);

		g_thread_yield ();
	}

	g_assert_cmpint (counter, ==, 100);

	/* Should realise the estimate is totally wrong by now! */
	iris_process_get_status (process_2, &processed_items, &total_items);
	g_assert_cmpint (total_items, ==, 100);

	g_object_unref (process_2);
}

static void
test_output_estimates_cancel (void)
{
	IrisProcess *process_1, *process_2;
	gint         j, wait_state = 0,
	             processed_items, total_items;

	process_1 = iris_process_new_with_func (push_next_func,
	                                        GINT_TO_POINTER (100000), NULL);
	process_2 = iris_process_new_with_func (wait_process_func,
	                                        &wait_state, NULL);
	iris_process_connect (process_1, process_2);
	g_object_ref (process_1);
	g_object_ref (process_2);

	iris_process_set_output_estimation (process_1, 100.0);

	/* Wait for connection to be processed */
	wait_control_messages (process_1);

	g_assert (iris_process_get_successor (process_1) == process_2);

	wait_state = 0;
	for (j=0; j<100; j++)
		iris_process_enqueue (process_1, iris_message_new (0));
	iris_process_no_more_work (process_1);

	/* Cancel the progress when it has more than 10 items queued (so it will
	 * estimate at least 1000 outputs) but before it completes.
	 */
	iris_process_run (process_1);
	do
		iris_process_get_status (process_1, &processed_items, &total_items);
	while (total_items <= 10);

	iris_process_cancel (process_1);
	wait_control_messages (process_1);

	/* Make sure the process didn't complete all its items, or there isn't
	 * much point in the test */
	g_assert (iris_process_get_queue_length (process_1) > 0);
	g_assert (! iris_process_has_succeeded (process_1));
	g_assert (iris_process_was_canceled (process_1));

	/* Wait for CHAIN_ESTIMATE messages to be processed */
	wait_control_messages (process_2);

	/* Because the head process was cancelled and not finished, the second
	 * should still trust its estimate. Therefore it should see at least 1000
	 * items from process_1's minimum of 10. This is required so that when a
	 * process chain being watched is cancelled the progress bar doesn't jump
	 * from the estimated value to the real value, which looks strange if
	 * they are very different.
	 */
	iris_process_get_status (process_2, &processed_items, &total_items);
	g_assert_cmpint (total_items, >, 1000);

	g_object_unref (process_1);
	g_object_unref (process_2);
}

int main(int argc, char *argv[]) {
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/process/lifecycle", test_lifecycle);
	g_test_add_func ("/process/simple", test_simple);
	g_test_add_func_repeated ("/process/cancel - creation", 50, test_cancel_creation);
	g_test_add_func_repeated ("/process/cancel - preparation", 50, test_cancel_preparation);
	g_test_add_func_repeated ("/process/cancel - execution 1", 50, test_cancel_execution_1);
	g_test_add_func_repeated ("/process/cancel - execution 2", 50, test_cancel_execution_2);
	g_test_add_func ("/process/cancel - execution 3", test_cancel_execution_3);

	g_test_add_func ("/process/titles", titles);

	g_test_add_func ("/process/recurse 1", recurse_1);
	g_test_add_func_repeated ("/process/chaining 1", 50, chaining_1);
	g_test_add_func ("/process/chaining 2", chaining_2);
	g_test_add_func ("/process/chaining 3", test_chaining_3);
	g_test_add_data_func_repeated ("/process/cancel chain - head",
	                               5,
	                               GINT_TO_POINTER (FALSE),
	                               test_cancelling_chained);
	g_test_add_data_func_repeated ("/process/cancel chain - tail",
	                               5,
	                               GINT_TO_POINTER (TRUE),
	                               test_cancelling_chained);
	g_test_add_func ("/process/canceling chained 2", test_cancel_before_chained);
	g_test_add_func_repeated ("/process/canceled chain is_finished()",
	                          50,
	                          test_canceled_chain_is_finished);

	g_test_add_func ("/process/output estimates basic", test_output_estimates_basic);
	g_test_add_func ("/process/output estimates low", test_output_estimates_low);
	g_test_add_func_repeated ("/process/output estimates cancel", 50, test_output_estimates_cancel);

	return g_test_run();
}
