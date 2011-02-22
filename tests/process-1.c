/* process-1.c
 *
 * Copyright (C) 2009 Sam Thursfield <ssssam@gmail.com>
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


static void
wait_messages (IrisProcess *process)
{
	IrisPort *port;

	port = IRIS_TASK(process)->priv->port;

	while (iris_port_get_queue_count (port) > 0 ||
	       g_atomic_int_get (&iris_port_get_receiver (port)->priv->active) > 0)
		g_thread_yield ();
}

static void
push_next_func (IrisProcess *process,
                IrisMessage *work_item,
                gpointer     user_data)
{
	gint delay = GPOINTER_TO_INT (user_data);

	iris_message_ref (work_item);
	iris_process_forward (process, work_item);

	if (delay)
		g_usleep (delay);
}

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
	g_usleep (1 * G_USEC_PER_SEC);
}

static void
enqueue_counter_work (IrisProcess *process,
                      gint        *p_counter,
                      gint         times)
{
	gint i;

	for (i=0; i < times; i++) {
		IrisMessage *work_item = iris_message_new (0);
		iris_message_set_pointer (work_item, "counter", p_counter);
		iris_process_enqueue (process, work_item);
	}
	iris_process_no_more_work (process);
}

static void
simple (void)
{
	gint counter = 0;

	IrisProcess *process = iris_process_new_with_func (counter_callback, NULL, NULL);

	iris_process_run (process);
	enqueue_counter_work (process, &counter, 50);

	while (!iris_process_is_finished (process))
		g_thread_yield ();

	g_assert_cmpint (counter, ==, 50);

	g_object_unref (process);
}

static void
test_has_succeeded (void)
{
	IrisProcess *process;
	gint         counter;

	/* Test TRUE on success */
	process = iris_process_new_with_func (counter_callback, NULL, NULL);
	counter = 0;
	enqueue_counter_work (process, &counter, 50);

	g_assert (iris_process_has_succeeded (process) == FALSE);

	iris_process_run (process);

	g_assert (iris_process_has_succeeded (process) == FALSE);

	while (!iris_process_is_finished (process))
		g_thread_yield ();

	g_assert (iris_process_has_succeeded (process) == TRUE);

	g_object_unref (process);

	/* Test FALSE on cancel */

	process = iris_process_new_with_func (counter_callback, NULL, NULL);
	counter = 0;
	enqueue_counter_work (process, &counter, 50);
	iris_process_run (process);

	iris_process_cancel (process);
	while (!iris_process_was_canceled (process))
		g_thread_yield ();

	g_assert (iris_process_has_succeeded (process) == FALSE);

	g_object_unref (process);
}

/* titles: Check the title property does not break */
static void
titles (void)
{
	gint  counter = 0,
	      i;
	char *title;

	IrisProcess *process = iris_process_new_with_func (counter_callback, NULL, NULL);
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

	while (!iris_process_is_finished (process))
		g_thread_yield ();

	g_assert_cmpint (counter, ==, 50);

	g_object_unref (process);
}


static void
recurse_1 (void)
{
	int counter = 0;

	IrisProcess *recursive_process = iris_process_new_with_func
	                                   (recursive_counter_callback, NULL, NULL);

	iris_process_run (recursive_process);

	IrisMessage *work_item = iris_message_new (0);
	iris_message_set_pointer (work_item, "counter", &counter);
	iris_process_enqueue (recursive_process, work_item);

	iris_process_no_more_work (recursive_process);

	while (!iris_process_is_finished (recursive_process))
		g_thread_yield ();

	g_assert_cmpint (counter, ==, 101);

	g_object_unref (recursive_process);
}

/* Cancel a process and spin waiting for it to be freed. Test fails if this
 * takes over 1 second.
 */
static void
cancelling_1 (void)
{
	int i;

	IrisProcess *process;

	process = iris_process_new_with_func (time_waster_callback, NULL, NULL);

	iris_process_run (process);

	for (i=0; i < 50; i++)
		iris_process_enqueue (process, iris_message_new (0));
	iris_process_no_more_work (process);

	iris_process_cancel (process);
	while (!iris_process_is_finished (process))
		g_thread_yield ();

	g_object_unref (process);
};

/* Cancel a process before running it. Should finish straight away. */
static void
cancelling_2 (void)
{
	IrisProcess *process;
	int          i;

	process = iris_process_new_with_func (time_waster_callback, NULL, NULL);
	iris_process_cancel (process);
	iris_process_run (process);

	for (i=0; i < 50; i++)
		iris_process_enqueue (process, iris_message_new (0));
	iris_process_no_more_work (process);

	while (!iris_process_is_finished (process))
		g_thread_yield ();

	g_object_unref (process);
};

/* Test that connect always executes before run. This behaviour is documented
 * in the manual for iris-process.c. More generally a test that messages are
 * received in the order they are sent. */
static void
chaining_1 (void)
{
	int i;

	for (i=0; i < 100; i++) {
		IrisProcess *head_process = iris_process_new_with_func
		                              (pre_counter_callback, NULL, NULL);
		IrisProcess *tail_process = iris_process_new_with_func
		                              (counter_callback, NULL, NULL);

		iris_process_connect (head_process, tail_process);

		iris_process_run (head_process);

		while (1) {
			g_usleep (10);
			if (iris_process_is_executing (head_process)) {
				g_assert (iris_process_has_successor (head_process));
				break;
			}
		}

		g_object_unref (head_process);
		g_object_unref (tail_process);
	}
}

static void
chaining_2 (void)
{
	int counter = 0,
	    i;
	
	IrisProcess *head_process = iris_process_new_with_func
	                              (pre_counter_callback, NULL, NULL);
	IrisProcess *tail_process = iris_process_new_with_func
	                              (counter_callback, NULL, NULL);

	iris_process_connect (head_process, tail_process);

	iris_process_run (head_process);

	for (i=0; i < 50; i++) {
		/* Set pointer as data instead of "counter" property, to ensure
		 * pre_counter_callback () is called to change it. */
		IrisMessage *work_item = iris_message_new_data (0, G_TYPE_POINTER, &counter);
		iris_process_enqueue (head_process, work_item);
	}
	iris_process_no_more_work (head_process);

	while (!iris_process_is_finished (tail_process))
		g_thread_yield ();

	g_assert_cmpint (counter, ==, 50);

	g_object_unref (head_process);
	g_object_unref (tail_process);
}

/* Cancel a chain and make sure they all exit */
static void
cancelling_3 (void) {
	int i;

	IrisProcess *head_process, *mid_process, *tail_process;

	head_process = iris_process_new_with_func (time_waster_callback, NULL,
	                                           NULL);
	mid_process  = iris_process_new_with_func (time_waster_callback, NULL,
	                                           NULL);
	tail_process = iris_process_new_with_func (time_waster_callback, NULL,
	                                           NULL);

	iris_process_connect (head_process, mid_process);
	iris_process_connect (mid_process, tail_process);

	iris_process_run (head_process);

	for (i=0; i < 50; i++)
		iris_process_enqueue (head_process, iris_message_new (0));
	iris_process_no_more_work (head_process);

	iris_process_cancel (tail_process);

	while (!iris_process_is_finished (tail_process))
		g_usleep (50);

	g_object_unref (head_process);
	g_object_unref (tail_process);
};

/* Cancel a process before chaining it to another. It's hard to see how this
 * could happen in real life. See also: cancelling 2.
 */
static void
cancelling_4 (void) {
	IrisProcess *head_process, *tail_process;

	head_process = iris_process_new_with_func (time_waster_callback, NULL,
	                                           NULL);
	tail_process = iris_process_new_with_func (time_waster_callback, NULL,
	                                           NULL);

	iris_process_cancel (head_process);
	iris_process_connect (head_process, tail_process);

	iris_process_run (head_process);

	while (!iris_process_is_finished (tail_process))
		g_thread_yield ();

	g_object_unref (head_process);
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
		if (i > 0)
			iris_process_connect (process[i-1], process[i]);
	}

	for (j=0; j<100; j++)
		iris_process_enqueue (process[0], iris_message_new (0));
	iris_process_run (process[0]);

	do
		iris_process_get_status (process[0], &processed_items, &total_items);
	while (total_items != 100);

	/* Let the message loop run a couple of times to the CHAIN_ESTIMATE message
	 * gets received. FIXME: might be better to wait by watching their
	 * communication channel ??
	 */
	g_usleep (10000);

	iris_process_get_status (process[1], &processed_items, &total_items);
	g_assert_cmpint (total_items, ==, 100);

	iris_process_get_status (process[2], &processed_items, &total_items);
	g_assert_cmpint (total_items, ==, 100);

	iris_process_cancel (process[0]);

	for (i=0; i<3; i++) {
		while (!iris_process_is_finished (process[i]))
			g_thread_yield ();
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

	g_object_unref (process_1);
	g_object_unref (process_2);
}

static void
test_output_estimates_cancel (void)
{
	IrisProcess *process_1, *process_2;
	gint         j, wait_state,
	             processed_items, total_items;

	process_1 = iris_process_new_with_func (push_next_func, NULL, NULL);
	process_2 = iris_process_new_with_func (wait_func, &wait_state, NULL);
	iris_process_connect (process_1, process_2);

	iris_process_set_output_estimation (process_1, 100.0);

	/* Wait for connection to be processed */
	wait_messages (process_1);

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

	g_assert (! iris_process_has_succeeded (process_1));

	/* Wait for CHAIN_ESTIMATE messages to be processed */
	wait_messages (process_2);


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

	g_test_add_func ("/process/simple", simple);
	g_test_add_func_repeated ("/process/has_succeeded()", 50, test_has_succeeded);

	g_test_add_func ("/process/titles", titles);

	g_test_add_func ("/process/cancelling 1", cancelling_1);
	g_test_add_func ("/process/cancelling 2", cancelling_2);
	g_test_add_func ("/process/recurse 1", recurse_1);
	g_test_add_func ("/process/chaining 1", chaining_1);
	g_test_add_func ("/process/chaining 2", chaining_2);
	g_test_add_func ("/process/cancelling 3", cancelling_3);
	g_test_add_func ("/process/cancelling 4", cancelling_4);

	g_test_add_func ("/process/output estimates basic", test_output_estimates_basic);
	g_test_add_func ("/process/output estimates low", test_output_estimates_low);
	g_test_add_func_repeated ("/process/output estimates cancel", 50, test_output_estimates_cancel);

	return g_test_run();
}
