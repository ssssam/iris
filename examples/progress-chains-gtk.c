/* progress-chains.c
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

/* progress-chains: tests watching connected processes. */

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gthread.h>

#include <iris.h>
#include <iris-gtk.h>

GtkWidget *demo_window = NULL;

GList *monitor_list = NULL;

const gint chain_size = 3;

const gchar *titles[2][3] =
  {
    { "Step 1: Aggregating previously unacceptable risks",
      "Step 2: ??",
      "Step 3: Profit",
    },
    { "Looking for odd socks",
      "Pairing",
      "Putting away"
    }
  };


static void
push_forward_func (IrisProcess *process,
                   IrisMessage *work_item,
                   gpointer     user_data)
{
	g_usleep (10000);

	if (iris_process_has_sink (process)) {
		iris_message_ref (work_item);
		iris_process_forward (process, work_item);
	}
}

/* Some fairly contrived example code, mainly to test output estimation */
static void
find_socks_func (IrisProcess *process,
                 IrisMessage *work_item,
                 gpointer     user_data)
{
	gint i;

	/* "Find" some socks */
	for (i=0; i<g_random_int_range (0, 20); i++) {
		g_usleep (1000);
		iris_process_forward (process, iris_message_new(i));
	}
}

static void
pair_socks_func (IrisProcess *process,
                 IrisMessage *work_item,
                 gpointer     user_data)
{
	g_usleep (10000);

	/* Forward every other item, more or less, because we have "paired" them */
	if (work_item->what & 1) {
		iris_message_ref (work_item);
		iris_process_forward (process, work_item);
	}
}

static void
trigger_process (GtkButton *trigger,
                 gpointer   user_data)
{
	IrisProcess *process[chain_size];
	GList       *node;
	gint         n, i;

	g_return_if_fail (chain_size >= 2);

	n = GPOINTER_TO_INT (user_data);

	if (n==1) {
		process[0] = iris_process_new_with_func (find_socks_func, NULL, NULL);
		process[1] = iris_process_new_with_func (pair_socks_func, NULL, NULL);
		process[2] = iris_process_new_with_func (push_forward_func, NULL, NULL);

		iris_task_set_progress_mode (IRIS_TASK (process[0]),
		                             IRIS_PROGRESS_ACTIVITY_ONLY);

		iris_process_set_output_estimation (process[1], 0.5);
	}

	for (i=0; i<chain_size; i++) {
		if (n==0) {
			process[i] = iris_process_new_with_func (push_forward_func, NULL, NULL);
			iris_task_set_progress_mode (IRIS_TASK (process[i]),
			                             IRIS_PROGRESS_CONTINUOUS);
		}

		iris_process_set_title (process[i], titles[n][i]);
	}

	for (i=1; i<chain_size; i++)
		iris_process_connect (process[i-1], process[i]);

	for (node=monitor_list; node; node=node->next) {
		IrisProgressMonitor *widget = IRIS_PROGRESS_MONITOR (node->data);
		IrisProgressGroup   *group;
		gint                 chain_middle;

		/* We add the middle process to make sure the progress monitor still
		 * gets the widgets in the right order.
		 */
		chain_middle = floor(chain_size / 2.0);
		group = g_object_get_data (G_OBJECT (widget), "group");
		iris_progress_monitor_watch_process_chain_in_group (widget,
		                                                    process[chain_middle],
		                                                    group);
	}

	for (i=0; i<500; i++)
		iris_process_enqueue (process[0], iris_message_new (0));
	iris_process_close (process[0]);

	iris_process_run (process[0]);
}

static void
create_demo_dialog (void)
{
	GtkWidget *vbox, *hbox,
	          *frame, *frame_label,
	          *button_1, *button_2;

	demo_window = gtk_dialog_new_with_buttons
	                ("Iris progress widgets demo", NULL, 0,
	                 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
	                 NULL);	                 
	g_signal_connect (demo_window,
	                  "response",
	                  G_CALLBACK (gtk_widget_destroy),
	                  NULL);
	g_signal_connect (demo_window,
	                  "destroy",
	                  G_CALLBACK (gtk_main_quit),
	                  NULL);
	gtk_window_set_default_size (GTK_WINDOW (demo_window), 480, 240);

	vbox = gtk_dialog_get_content_area (GTK_DIALOG (demo_window));

	frame = gtk_frame_new ("");
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

	frame_label = gtk_frame_get_label_widget (GTK_FRAME (frame));
	gtk_label_set_markup (GTK_LABEL (frame_label), "<b>Create a chain of processes</b>");

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 12);

	button_1 = gtk_button_new_with_label ("Plan");
	g_signal_connect (button_1, "clicked",
	                  G_CALLBACK (trigger_process), GINT_TO_POINTER (0));

	button_2 = gtk_button_new_with_label ("Action");
	g_signal_connect (button_2, "clicked",
	                  G_CALLBACK (trigger_process), GINT_TO_POINTER (1));

	gtk_box_pack_start (GTK_BOX (hbox), button_1, TRUE, TRUE, 24);
	gtk_box_pack_start (GTK_BOX (hbox), button_2, TRUE, TRUE, 24);
	gtk_container_add (GTK_CONTAINER (frame), hbox);

	gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, TRUE, 4);
}

static void
create_group (IrisProgressMonitor *progress_monitor)
{
	IrisProgressGroup *group;

	group = iris_progress_monitor_add_group (progress_monitor,
	                                         "Progress chaining example", NULL);

	g_object_set_data (G_OBJECT (progress_monitor), "group", group);
}

static void
create_progress_monitors (void)
{
	GtkWidget *dialog, *info_bar,
	          *vbox;

	g_return_if_fail (demo_window != NULL);

	/* The dialog */
	dialog = gtk_iris_progress_dialog_new (GTK_WINDOW (demo_window));
	iris_progress_monitor_set_permanent_mode (IRIS_PROGRESS_MONITOR (dialog),
	                                          TRUE);
	monitor_list = g_list_prepend (monitor_list, dialog);
	create_group (IRIS_PROGRESS_MONITOR (dialog));

	/* Delete is triggered if the user presses 'esc' in the progress dialog */
	g_signal_connect (dialog,
	                  "delete-event",
	                  G_CALLBACK (gtk_widget_hide_on_delete),
	                  NULL);

	/* The info bar */
	info_bar = gtk_iris_progress_info_bar_new ();

	vbox = gtk_dialog_get_content_area (GTK_DIALOG (demo_window));
	gtk_box_pack_end (GTK_BOX (vbox), info_bar, FALSE, TRUE, 0);
	gtk_widget_hide (info_bar);

	/* Without this reference the info bar is destroyed by the dialog on close,
	 * due to the call to gtk_widget_destroy().
	 */
	g_object_ref (info_bar);

	iris_progress_monitor_set_permanent_mode (IRIS_PROGRESS_MONITOR (info_bar),
	                                          TRUE);
	monitor_list = g_list_prepend (monitor_list, info_bar);
	create_group (IRIS_PROGRESS_MONITOR (info_bar));
}

gint
main (gint argc, char *argv[])
{
	GList *node;

	g_thread_init (NULL);
	gtk_init (&argc, &argv);

	create_demo_dialog ();
	g_signal_connect (demo_window, "response", gtk_main_quit, NULL);
	gtk_widget_show_all (demo_window);

	create_progress_monitors ();

	/* Main loop */
	gtk_main ();

	for (node=monitor_list; node; node=node->next) {
		IrisProgressGroup *group;

		g_assert_cmpint (G_OBJECT (node->data)->ref_count, ==, 1);

		/* We don't cancel the processes, so there could be other references on
		 * the group that they still hold.
		 */
		group = g_object_get_data (G_OBJECT (node->data), "group");
		iris_progress_group_unref (group);

		gtk_widget_destroy (GTK_WIDGET (node->data));
	}

	return 0;
}
