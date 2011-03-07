/* progress-monitor-gtk-1.c
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
 
/* progress-monitor-gtk-1: tests implementations of IrisProgressMonitor. */
 
#include <stdlib.h>
#include <gtk/gtk.h>
#include <iris.h>
#include <iris-gtk.h>

#include "iris/iris-process-private.h"
#include "iris-gtk/gtk-iris-progress-dialog-private.h"
#include "iris-gtk/gtk-iris-progress-info-bar-private.h"

/* FIXME: surely you could write a mock progress monitor for some of these things */

/* Work item to increment a global counter, so we can tell if the hole queue
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

	/* Add 2 new work items, up to 100 */
	if (*counter_address < 50) {
		for (i=0; i<2; i++) {
			IrisMessage *new_work_item = iris_message_new (0);
			iris_message_set_pointer (new_work_item, "counter", counter_address);
			iris_process_recurse (process, new_work_item);
		}
	}

	(*counter_address) ++;
}

/*static void
time_waster_callback (IrisProcess *process,
                      IrisMessage *work_item,
                      gpointer     user_data)
{
	g_usleep (1 * G_USEC_PER_SEC);
}*/

static void
count_sheep_func (IrisProcess *process,
                  IrisMessage *work_item,
                  gpointer     user_data)
{
	/* There's one! */
	g_usleep (10000);
}


/* From examples/progress-tasks.c, code for a task that will send appropriate messages for
 * IrisProgressMonitor */
static void
thinking_task_func (IrisTask *task,
                    gpointer  user_data)
{
	IrisMessage *status_message;
	IrisPort    *watch_port = g_object_get_data (G_OBJECT (task), "watch-port");
	const gint   count = GPOINTER_TO_INT (user_data);
	gboolean     cancelled = FALSE;
	gint         i;

	for (i=0; i<count; i++) {
		g_usleep (1000);

		if (iris_task_was_canceled (task)) {
			cancelled = TRUE;
			break;
		}

		status_message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_FRACTION,
		                                        G_TYPE_FLOAT,
		                                        (float)i/(float)count);
		iris_port_post (watch_port, status_message);
	}

	if (cancelled) {
		status_message = iris_message_new (IRIS_PROGRESS_MESSAGE_CANCELED);
	} else {
		status_message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_FRACTION,
		                                        G_TYPE_FLOAT, 1.0);
		iris_port_post (watch_port, status_message);

		status_message = iris_message_new (IRIS_PROGRESS_MESSAGE_COMPLETE);
	}
	iris_port_post (watch_port, status_message);
}


static void
counter_enqueue (IrisProcess *process,
                 gint        *counter,
                 gint         count)
{
	int i;

	for (i=0; i < count; i++) {
		IrisMessage *work_item = iris_message_new (0);
		iris_message_set_pointer (work_item, "counter", counter);
		iris_process_enqueue (process, work_item);
	}
}

typedef struct {
	GMainLoop           *main_loop;
	GtkWidget           *container;
	IrisProgressMonitor *monitor;
} ProgressFixture;

static void
gtk_iris_progress_dialog_fixture_setup (ProgressFixture *fixture,
                                    gconstpointer    user_data)
{
	GtkWidget *dialog;

	fixture->main_loop = g_main_loop_new (NULL, TRUE);
	fixture->container = NULL;

	dialog = gtk_iris_progress_dialog_new (NULL);

	g_assert (GTK_IRIS_IS_PROGRESS_DIALOG (dialog));

	fixture->monitor = IRIS_PROGRESS_MONITOR (dialog);

	gtk_widget_show (dialog);
}

static void
gtk_iris_progress_dialog_fixture_teardown (ProgressFixture *fixture,
                                       gconstpointer    user_data)
{
	if (fixture->monitor != NULL)
		gtk_widget_destroy (GTK_WIDGET (fixture->monitor));

	g_main_loop_unref (fixture->main_loop);
}

static void
gtk_iris_progress_info_bar_fixture_setup (ProgressFixture *fixture,
                                      gconstpointer    user_data)
{
	GtkWidget *info_bar;

	fixture->main_loop = g_main_loop_new (NULL, TRUE);

	/* Container window so we can display the infobar; it's not a great test if the
	 * widget is not even realized.
	 */
	fixture->container = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	info_bar = gtk_iris_progress_info_bar_new ();

	g_assert (GTK_IRIS_IS_PROGRESS_INFO_BAR (info_bar));

	fixture->monitor = IRIS_PROGRESS_MONITOR (info_bar);

	gtk_container_add (GTK_CONTAINER (fixture->container), GTK_WIDGET (fixture->monitor));

	gtk_widget_show (info_bar);
	gtk_widget_show (fixture->container);
}

static void
gtk_iris_progress_info_bar_fixture_teardown (ProgressFixture *fixture,
                                         gconstpointer    user_data)
{
	if (fixture->monitor != NULL)
		gtk_widget_destroy (GTK_WIDGET (fixture->monitor));

	gtk_widget_destroy (fixture->container);

	g_main_loop_unref (fixture->main_loop);
}

static const gchar*
ui_get_watch_title (IrisProgressMonitor *monitor,
                    IrisTask            *watch_task)
{
	IrisProgressMonitorInterface *iface;
	IrisProgressWatch            *watch;

	iface = IRIS_PROGRESS_MONITOR_GET_INTERFACE (monitor);
	watch = iface->get_watch (monitor, watch_task);

	if (GTK_IRIS_IS_PROGRESS_DIALOG (monitor))
		return gtk_label_get_text (watch->title_label);
	else
	if (GTK_IRIS_IS_PROGRESS_INFO_BAR (monitor))
		return gtk_label_get_text (watch->title_label);

	g_warn_if_reached ();
	return NULL;
}

static void
simple (ProgressFixture *fixture,
        gconstpointer data)
{
	gint counter = 0;

	IrisProcess *process = iris_process_new_with_func (counter_callback, NULL,
	                                                   NULL);
	g_object_ref (process);
	iris_progress_monitor_watch_process (fixture->monitor, process, 0);
	iris_process_run (process);

	counter_enqueue (process, &counter, 50);
	iris_process_no_more_work (process);

	while (!iris_process_is_finished (process)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}
	g_object_unref (process);
}


/* process titles 1: test that NULL titles and title updates don't break
 *                   anything
 */
static void
process_titles_1 (ProgressFixture *fixture,
                  gconstpointer data)
{
	gint counter = 0;

	/* A process with no title */
	IrisProcess *process = iris_process_new_with_func (count_sheep_func,
	                                                   NULL, NULL);
	g_object_ref (process);
	iris_progress_monitor_watch_process (fixture->monitor, process, 0);
	iris_process_run (process);

	counter_enqueue (process, &counter, 50);

	/* Check the title change message doesn't crash anything */
	iris_process_set_title (process, "Test title");
	iris_process_no_more_work (process);

	while (!iris_process_is_finished (process)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	g_object_unref (process);
}


/* process titles 2: test that UI is updated on changes */
static void
process_titles_2 (ProgressFixture *fixture,
                  gconstpointer    data)
{
	gint               counter = 0,
	                   i;
	IrisProcess       *process_1,
	                  *process_2;

	const char        *ui_title_1 = NULL,
	                  *ui_title_2 = NULL;

	/* Don't remove watches so we can test their UI elements */
	iris_progress_monitor_set_watch_hide_delay (fixture->monitor, -1);

	process_1 = iris_process_new_with_func (count_sheep_func,
	                                        NULL, NULL);
	g_object_ref (process_1);
	iris_process_set_title (process_1, "Test");
	iris_progress_monitor_watch_process (fixture->monitor, process_1, 0);
	counter_enqueue (process_1, &counter, 50);
	iris_process_run (process_1);

	/* Delay before adding the second process. We are aiming to trigger a bug
	 * where the 'ADD_WATCH' message to process_2 gets delayed, so when the
	 * title changes, below, it does not notify the progress monitor.
	 */

	for (i=0; i<50; i++) {
		g_usleep (50);
		g_main_context_iteration (NULL, FALSE);
	}

	process_2 = iris_process_new_with_func (count_sheep_func,
	                                        NULL, NULL);
	g_object_ref (process_2);
	iris_process_set_title (process_2, "Test");
	iris_progress_monitor_watch_process (fixture->monitor, process_2, 0);

	counter_enqueue (process_2, &counter, 50);
	iris_process_run (process_2);

	iris_process_set_title (process_1, "New Title");
	iris_process_no_more_work (process_1);

	iris_process_set_title (process_2, "New Title");
	iris_process_no_more_work (process_2);

	while (!iris_process_is_finished (process_2)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	/* Check that the UI reflects the title */
	ui_title_1 = ui_get_watch_title (fixture->monitor,
	                                 IRIS_TASK (process_1));
	ui_title_2 = ui_get_watch_title (fixture->monitor,
	                                 IRIS_TASK (process_2));

	g_assert_cmpstr (ui_title_1, ==, "New Title");
	g_assert_cmpstr (ui_title_2, ==, "New Title");

	g_object_unref (process_1);
	g_object_unref (process_2);
}

/* process titles 3: test process can change its title after completion
 *                   (more so nothing breaks than because it's a good use case)
 */
static void
process_titles_3 (ProgressFixture *fixture,
                  gconstpointer    data)
{
	IrisProcess *process;
	const gchar *ui_title;
	gint         i;

	iris_progress_monitor_set_watch_hide_delay (fixture->monitor, -1);

	process = iris_process_new_with_func (count_sheep_func,
	                                      NULL, NULL);
	g_object_ref (process);
	iris_process_set_title (process, "tseT");
	iris_progress_monitor_watch_process (fixture->monitor, process, 0);
	iris_process_run (process);
	iris_process_no_more_work (process);

	while (!iris_process_is_finished (process)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	iris_process_set_title (process, "Test");

	/* Let the spurious message deliver, if there was one */
	for (i=0; i<100; i++) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	ui_title = ui_get_watch_title (fixture->monitor, IRIS_TASK (process));
	g_assert_cmpstr (ui_title, ==, "Test");

	g_object_unref (process);
}

/* recurse 1: basic check for recursive processes */
static void
recurse_1 (ProgressFixture *fixture,
           gconstpointer data)
{
	int counter = 0;

	IrisProcess *recursive_process = iris_process_new_with_func
	                                   (recursive_counter_callback, NULL, NULL);
	g_object_ref (recursive_process);
	iris_progress_monitor_watch_process (fixture->monitor, 
	                                     recursive_process,
	                                     0);
	iris_process_run (recursive_process);

	IrisMessage *work_item = iris_message_new (0);
	iris_message_set_pointer (work_item, "counter", &counter);
	iris_process_enqueue (recursive_process, work_item);

	iris_process_no_more_work (recursive_process);

	while (!iris_process_is_finished (recursive_process)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	/* Processing must have finished if _is_finished()==TRUE. In particular
	 * IRIS_PROGRESS_MESSAGE_FINISH is always sent before the process is flagged as finished,
	 * so the watch will not be left hanging.
	 */
	g_object_unref (recursive_process);
}

/* chaining 1: basic test for chaining processes */
static void
chaining_1 (ProgressFixture *fixture,
            gconstpointer data)
{
	int counter = 0,
	    i;
	
	IrisProcess *head_process = iris_process_new_with_func
	                              (pre_counter_callback, NULL, NULL);
	IrisProcess *tail_process = iris_process_new_with_func
	                              (counter_callback, NULL, NULL);
	g_object_ref (tail_process);

	iris_process_connect (head_process, tail_process);
	iris_progress_monitor_watch_process_chain (fixture->monitor,
	                                           head_process,
	                                           "Test Group",
	                                           NULL);
	iris_process_run (head_process);

	for (i=0; i < 50; i++) {
		/* Set pointer as data instead of "counter" property, to ensure
		 * pre_counter_callback () is called to change it. */
		IrisMessage *work_item = iris_message_new_data (0, G_TYPE_POINTER, &counter);
		iris_process_enqueue (head_process, work_item);
	}
	iris_process_no_more_work (head_process);

	while (!iris_process_is_finished (tail_process)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	g_object_unref (tail_process);
}

/* cancel chain: test cancel for a process group works cleanly */
static void
test_cancel_chain (ProgressFixture *fixture,
                   gconstpointer    data)
{
	IrisProgressGroup *group;

	IrisProcess *head_process = iris_process_new_with_func
	                              (NULL, NULL, NULL);
	IrisProcess *tail_process = iris_process_new_with_func
	                              (NULL, NULL, NULL);
	iris_process_connect (head_process, tail_process);
	iris_process_no_more_work (head_process);
	g_object_ref (tail_process);

	group = iris_progress_monitor_add_group (fixture->monitor, "Test Group", NULL);
	iris_progress_monitor_watch_process_chain_in_group (fixture->monitor,
	                                                    head_process,
	                                                    group);

	_iris_progress_monitor_cancel_group (fixture->monitor, group);

	while (!iris_process_is_finished (tail_process)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	g_assert (iris_process_was_canceled (tail_process) == TRUE);

	g_object_unref (tail_process);
}

/* finished 1: run several processes under the same monitor, all completing
 *             quickly. This tests the progress monitor emits 'finished' at
 *             the right time: too early and segfaults occur.
 */
static void
finished_handler (IrisProgressMonitor *progress_monitor,
                  gpointer             user_data) {
	gboolean *finished_has_executed = (gboolean *)user_data;

	g_assert ((*finished_has_executed) == FALSE);

	*finished_has_executed = TRUE;
}

static void
finished_1 (ProgressFixture *fixture,
            gconstpointer    data)
{
	IrisProcess *process;
	gboolean     finished_has_executed = FALSE;
	int          i, j;

	g_signal_connect (G_OBJECT (fixture->monitor),
	                  "finished",
	                  G_CALLBACK (finished_handler),
	                  &finished_has_executed);

	for (i=0; i<4; i++) {
		process = iris_process_new_with_func (count_sheep_func, NULL, NULL);

		iris_progress_monitor_watch_process (fixture->monitor,
		                                     process,
		                                     0);

		for (j=0; j<10; j++)
			iris_process_enqueue (process, iris_message_new (0));
		iris_process_no_more_work (process);

		iris_process_run (process);
	}

	while (finished_has_executed == FALSE) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}
}

/* finalize: test that destroying the progress monitor while watches are
 *           running does not crash anything (although it's not really good
 *           practice to do this)
 */
static void
finalize (ProgressFixture *fixture,
          gconstpointer    data)
{
	IrisProcess *process;
	int          i, j;

	for (i=0; i<4; i++) {
		process = iris_process_new_with_func (count_sheep_func, NULL, NULL);

		iris_progress_monitor_watch_process (fixture->monitor, process, 0);

		for (j=0; j<100; j++)
			iris_process_enqueue (process, iris_message_new (0));
		iris_process_no_more_work (process);

		iris_process_run (process);
	}

	gtk_widget_destroy (GTK_WIDGET (fixture->monitor));

	fixture->monitor = FALSE;
}


/* recurse_2: potentially recursive process with only 1 work item, if there is
 *            any danger of progress update signals being missed this will
 *            hopefully trigger the bugs
 */
static void
recurse_2_head_callback (IrisProcess *process,
                         IrisMessage *work_item,
                         gpointer     user_data) {
	IrisMessage *new_work_item;

	g_assert (iris_process_has_successor (process));

	new_work_item = iris_message_new (2);
	iris_process_forward (process, new_work_item);
}

static void
recurse_2 (ProgressFixture *fixture,
           gconstpointer    data) {
	IrisProcess *head_process, *tail_process;
	IrisMessage *item;

	g_object_add_weak_pointer (G_OBJECT (fixture->monitor),
	                           (gpointer *) &fixture->monitor);
	g_signal_connect (fixture->monitor,
	                  "finished",
	                  G_CALLBACK (gtk_widget_destroy),
	                  NULL);

	head_process = iris_process_new_with_func (recurse_2_head_callback, NULL, NULL);

	item = iris_message_new (1);
	iris_process_enqueue (head_process, item);

	tail_process = iris_process_new_with_func (count_sheep_func, NULL, NULL);

	iris_process_connect (head_process, tail_process);
	iris_process_no_more_work (head_process);
	iris_process_run (head_process);

	iris_progress_monitor_watch_process_chain (IRIS_PROGRESS_MONITOR(fixture->monitor),
	                                           head_process,
	                                           "Recursion Test",
	                                           NULL);

	while (fixture->monitor != NULL) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}
}

/* tasks: check that progress monitors don't only work for IrisProcess */
static void
tasks (ProgressFixture *fixture,
       gconstpointer    data)
{
	IrisTask *task;
	IrisPort *watch_port;

	task = iris_task_new_with_func (thinking_task_func, GINT_TO_POINTER (100), NULL);

	watch_port = iris_progress_monitor_add_watch
	               (IRIS_PROGRESS_MONITOR (fixture->monitor),
	                task,
	                "Test",
	                0);
	g_object_set_data (G_OBJECT (task), "watch-port", watch_port);

	g_object_add_weak_pointer (G_OBJECT (fixture->monitor),
	                           (gpointer *) &fixture->monitor);
	g_signal_connect (fixture->monitor,
	                  "finished",
	                  G_CALLBACK (gtk_widget_destroy),
	                  NULL);

	iris_task_run (task);

	while (fixture->monitor != NULL) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}
}

/* cancelling: test the widget's cancel button */
/*
static void
cancelling (ProgressFixture *fixture,
            gconstpointer data)
{
	int i;

	IrisProcess *process;

	process = iris_process_new_with_func (time_waster_callback, NULL, NULL);
	iris_progress_monitor_watch_process (fixture->monitor, process);
	iris_process_run (process);

	for (i=0; i < 50; i++)
		iris_process_enqueue (process, iris_message_new (0));

	progress_fixture_cancel (fixture);

	while (!iris_process_is_finished (process)) {
		g_thread_yield ();
		g_main_context_iteration (NULL, FALSE);
	}

	g_object_unref (process);
};
*/

static void
add_tests_with_fixture (void (*setup) (ProgressFixture *, gconstpointer),
                        void (*teardown) (ProgressFixture *, gconstpointer),
                        const char *name)
{
	char buf[256];

	g_snprintf (buf, 255, "/progress-monitor/%s/simple", name);
	g_test_add (buf, ProgressFixture, NULL, setup, simple, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/process titles 1", name);
	g_test_add (buf, ProgressFixture, NULL, setup, process_titles_1, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/process titles 2", name);
	g_test_add (buf, ProgressFixture, NULL, setup, process_titles_2, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/process titles 3", name);
	g_test_add (buf, ProgressFixture, NULL, setup, process_titles_3, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/recurse 1", name);
	g_test_add (buf, ProgressFixture, NULL, setup, recurse_1, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/chaining 1", name);
	g_test_add (buf, ProgressFixture, NULL, setup, chaining_1, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/cancel chain", name);
	g_test_add (buf, ProgressFixture, NULL, setup, test_cancel_chain, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/finished 1", name);
	g_test_add (buf, ProgressFixture, NULL, setup, finished_1, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/finalize", name);
	g_test_add (buf, ProgressFixture, NULL, setup, finalize, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/recurse 2", name);
	g_test_add (buf, ProgressFixture, NULL, setup, recurse_2, teardown);

	g_snprintf (buf, 255, "/progress-monitor/%s/tasks", name);
	g_test_add (buf, ProgressFixture, NULL, setup, tasks, teardown);

//	g_snprintf (buf, 255, "/progress-monitor/%s/cancelling", name);
//	g_test_add (buf, ProgressFixture, NULL, setup, cancelling, teardown);
}

int main(int argc, char *argv[]) {
	g_thread_init (NULL);
	gtk_test_init (&argc, &argv, NULL);

	add_tests_with_fixture (gtk_iris_progress_dialog_fixture_setup,
	                        gtk_iris_progress_dialog_fixture_teardown,
	                        "dialog");
	add_tests_with_fixture (gtk_iris_progress_info_bar_fixture_setup,
	                        gtk_iris_progress_info_bar_fixture_teardown,
	                        "info-bar");

	return g_test_run();
}
