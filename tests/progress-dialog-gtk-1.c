/* progress-dialog-gtk-1.c
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
 
/* progress-dialog-gtk-1: tests for IrisProgressDialog only.
 * 
 * FIXME: some of these tests may fail just because of computer slowness; eg. we call
 * g_main_context_iteration() a bunch of times and then hope that UI updates have propagated.
 * Although there may be a bug issue at work if they haven't, perhaps it would be better if these
 * tests would never fail jus due to slowness, and if we need performance regression testing it
 * could be achieved in a more reliable way.
 */
 
#include <stdlib.h>
#include <gtk/gtk.h>
#include <iris.h>
#include <iris-gtk.h>

#include "iris/iris-process-private.h"
#include "iris-gtk/iris-progress-dialog-private.h"

static void
push_next_func (IrisProcess *process,
                IrisMessage *work_item,
                gpointer     user_data)
{
	iris_message_ref (work_item);
	iris_process_forward (process, work_item);
}

/* wait_func: hold up the process until a flag is set to continue; useful for
 *            testing progress UI
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

static void
main_loop_iteration_times (gint times)
{
	gint i;

	for (i=0; i<times; i++) {
		g_main_context_iteration (NULL, FALSE);
		g_usleep (50);
		g_thread_yield ();
	}
}

typedef struct {
	GMainLoop           *main_loop;
	GtkWidget           *container;
	IrisProgressMonitor *monitor;
} ProgressFixture;

static void
iris_progress_dialog_fixture_setup (ProgressFixture *fixture,
                                    gconstpointer    user_data)
{
	GtkWidget *dialog;

	fixture->main_loop = g_main_loop_new (NULL, TRUE);
	fixture->container = NULL;

	dialog = iris_progress_dialog_new (NULL);

	g_assert (IRIS_IS_PROGRESS_DIALOG (dialog));

	fixture->monitor = IRIS_PROGRESS_MONITOR (dialog);

	gtk_widget_show (dialog);
}

static void
iris_progress_dialog_fixture_teardown (ProgressFixture *fixture,
                                       gconstpointer    user_data)
{
	if (fixture->monitor != NULL)
		gtk_widget_destroy (GTK_WIDGET (fixture->monitor));

	g_main_loop_unref (fixture->main_loop);
}

/* default title: basic test of dialog titles */
static void
default_title (ProgressFixture *fixture,
                      gconstpointer    data)
{
	IrisMessage *work_item;
	gint         wait_state = 0;
	const gchar *title;

	/* FIXME: presumably this text will fail in non-EN locales once Iris is
	 * translated, unless tests are fixed to that locale, which I hope they are
	 */

	/* Check default empty title */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Progress Monitor");

	IrisProcess *process = iris_process_new_with_func (wait_func,
	                                                   &wait_state, NULL);
	iris_progress_monitor_watch_process (fixture->monitor,
	                                     process,
	                                     IRIS_PROGRESS_MONITOR_PERCENTAGE,
	                                     0);
	iris_progress_monitor_set_watch_hide_delay (fixture->monitor, 0);
	iris_process_run (process);

	work_item = iris_message_new (1000);
	iris_process_enqueue (process, work_item);
	work_item = iris_message_new (1001);
	iris_process_enqueue (process, work_item);
	iris_process_no_more_work (process);

	main_loop_iteration_times (100);

	/* Check default title when process has no title */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "0% complete");

	iris_process_set_title (process, "Test process");

	main_loop_iteration_times (10);

	/* Check default title when process has no title */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test process - 0% complete");

	g_atomic_int_set (&wait_state, 2);

	do
		main_loop_iteration_times (500);
	while (!iris_process_is_finished (process));

	/* Check default empty title returns after all processes complete */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Progress Monitor");

	g_object_unref (process);
}


/* custom static title: test static titles do not get destroyed */
static void
custom_static_title (ProgressFixture *fixture,
                      gconstpointer    data)
{
	IrisMessage *work_item;
	gint         wait_state = 0;
	const gchar *title;

	iris_progress_dialog_set_title (IRIS_PROGRESS_DIALOG (fixture->monitor),
	                                "Test");

	/* Check default empty title */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test");

	IrisProcess *process = iris_process_new_with_func (wait_func,
	                                                   &wait_state, NULL);
	iris_progress_monitor_watch_process (fixture->monitor,
	                                     process,
	                                     IRIS_PROGRESS_MONITOR_PERCENTAGE,
	                                     0);
	iris_progress_monitor_set_watch_hide_delay (fixture->monitor, 0);
	iris_process_run (process);

	work_item = iris_message_new (2000);
	iris_process_enqueue (process, work_item);
	work_item = iris_message_new (2001);
	iris_process_enqueue (process, work_item);
	iris_process_no_more_work (process);

	main_loop_iteration_times (100);

	iris_process_set_title (process, "Test process");

	/* Check progress info has not destroyed the title */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test");

	g_atomic_int_set (&wait_state, 2);

	g_object_unref (process);
}

/* custom dynamic title: test custom titles work including progress info */
static void
custom_dynamic_title (ProgressFixture *fixture,
                      gconstpointer    data)
{
	IrisMessage *work_item;
	gint         wait_state = 0;
	const gchar *title;

	iris_progress_dialog_set_title (IRIS_PROGRESS_DIALOG (fixture->monitor),
	                                "Test %s Test");

	/* Check default empty title */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test Progress Monitor Test");

	IrisProcess *process = iris_process_new_with_func (wait_func,
	                                                   &wait_state, NULL);
	iris_progress_monitor_watch_process (fixture->monitor,
	                                     process,
	                                     IRIS_PROGRESS_MONITOR_PERCENTAGE,
	                                     0);
	iris_progress_monitor_set_watch_hide_delay (fixture->monitor, 0);

	work_item = iris_message_new (0);
	iris_process_enqueue (process, work_item);
	work_item = iris_message_new (0);
	iris_process_enqueue (process, work_item);
	iris_process_no_more_work (process);

	iris_process_run (process);

	main_loop_iteration_times (1000);

	/* Check default title when process has no title */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test 0% complete Test");

	iris_process_set_title (process, "Test process");

	main_loop_iteration_times (10);

	/* Check default title when process is titled */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test Test process - 0% complete Test");

	g_atomic_int_set (&wait_state, 2);

	do
		main_loop_iteration_times (500);
	while (!iris_process_is_finished (process));
	main_loop_iteration_times (500);

	/* Check default empty title returns after all processes complete */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test Progress Monitor Test");

	g_object_unref (process);
}


/* groups: watch title should come from first group being watched */
static void
test_groups (ProgressFixture *fixture,
             gconstpointer    data)
{
	IrisMessage *work_item;
	gint         wait_state = 0;
	const gchar *title;

	IrisProcess *process_head, *process_tail;
	
	process_head = iris_process_new_with_func (push_next_func, NULL, NULL);
	process_tail = iris_process_new_with_func (wait_func, &wait_state, NULL);

	iris_process_connect (process_head, process_tail);

	iris_progress_monitor_watch_process_chain (fixture->monitor,
	                                           process_head,
	                                           IRIS_PROGRESS_MONITOR_ITEMS,
	                                           "Test Group",
	                                           NULL);
	iris_progress_monitor_set_watch_hide_delay (fixture->monitor, 0);

	work_item = iris_message_new (0);
	iris_process_enqueue (process_head, work_item);
	work_item = iris_message_new (0);
	iris_process_enqueue (process_head, work_item);
	iris_process_no_more_work (process_head);

	iris_process_run (process_head);

	while (g_atomic_int_get (&wait_state) != 1) {
		main_loop_iteration_times (500);
		g_thread_yield ();
	}
	main_loop_iteration_times (500);

	/* Check group progress is shown */
	title = gtk_window_get_title (GTK_WINDOW (fixture->monitor));
	g_assert_cmpstr (title, ==, "Test Group - 75% complete");

	g_atomic_int_set (&wait_state, 2);

	while (!iris_process_is_finished (process_tail))
		main_loop_iteration_times (100);

	g_object_unref (process_head);
	g_object_unref (process_tail);
}


int main(int argc, char *argv[]) {
	g_thread_init (NULL);
	gtk_test_init (&argc, &argv, NULL);

	g_test_add ("/progress-dialog/default title",
	            ProgressFixture,
	            NULL,
	            iris_progress_dialog_fixture_setup,
	            default_title,
	            iris_progress_dialog_fixture_teardown);

	g_test_add ("/progress-dialog/custom static title",
	            ProgressFixture,
	            NULL,
	            iris_progress_dialog_fixture_setup,
	            custom_static_title,
	            iris_progress_dialog_fixture_teardown);

	g_test_add ("/progress-dialog/custom dynamic title",
	            ProgressFixture,
	            NULL,
	            iris_progress_dialog_fixture_setup,
	            custom_dynamic_title,
	            iris_progress_dialog_fixture_teardown);

	g_test_add ("/progress-dialog/groups",
	            ProgressFixture,
	            NULL,
	            iris_progress_dialog_fixture_setup,
	            test_groups,
	            iris_progress_dialog_fixture_teardown);

	return g_test_run();
}
