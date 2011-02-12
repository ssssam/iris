/* progress-groups.c
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

/* progress-groups: demonstrates watch grouping. */

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gthread.h>

#include <iris.h>
#include <iris-gtk.h>

#include <iris/iris-progress-monitor-private.h>

GtkWidget *demo_window = NULL;

GList *monitor_list = NULL;

const gint n_titles = 10;

const gchar *titles[] =
  { "Counting sheep",
    "Compiling",
    "Updating to-do list",
    "Checking email",
    "Checking Facebook",
    "Reading the news",
    "Waiting for Godot",
    "Trying to arrange an alibi for last friday",
    "AFK",
    "Procrastinating"
  };


static void
count_sheep_func (IrisProcess *process,
                  IrisMessage *work_item,
                  gpointer     user_data)
{
	g_usleep (10000);
}

static void
trigger_process (GtkButton *trigger,
                 gpointer   user_data)
{
	IrisProcess *process;
	gint         i,
	             group_n;
	GList       *node;

	process = iris_process_new_with_func (count_sheep_func, NULL, NULL);

	i = g_random_int_range (0, 10);
	iris_process_set_title (process, titles[i]);

	group_n = GPOINTER_TO_INT (user_data);
	g_return_if_fail (group_n <= 4);

	for (node=monitor_list; node; node=node->next) {
		IrisProgressMonitor *widget = IRIS_PROGRESS_MONITOR (node->data);
		IrisProgressGroup   **groups,
		                    *group = NULL;

		if (group_n > 0) {
			groups = g_object_get_data (G_OBJECT (widget), "groups");
			group = groups[group_n - 1];
		}

		iris_progress_monitor_watch_process (widget,
		                                     process,
		                                     IRIS_PROGRESS_MONITOR_PERCENTAGE,
		                                     group);
	}

	for (i=0; i<500; i++)
		iris_process_enqueue (process, iris_message_new (0));
	iris_process_no_more_work (process);

	iris_process_run (process);
}

static void
create_demo_dialog (void)
{
	GtkWidget *vbox, *hbox,
	          *frame, *frame_label,
	          *triggers_box, *button;

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
	gtk_label_set_markup (GTK_LABEL (frame_label), "<b>Create new process</b>");

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 12);

	triggers_box = gtk_hbutton_box_new ();
	gtk_button_box_set_spacing (triggers_box, 8);

	button = gtk_button_new_with_label ("Red Team");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (1));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label ("Blue Team");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (2));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label ("Chartreuse Team");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (3));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label ("Myrtle Team");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (4));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label ("No Team");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (0));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (hbox), triggers_box, TRUE, TRUE, 24);
	gtk_container_add (GTK_CONTAINER (frame), hbox);

	gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, TRUE, 4);
}

static void
create_groups (IrisProgressMonitor *progress_monitor)
{
	IrisProgressGroup **data = g_new (IrisProgressGroup *, 4);

	/* We create the 4 groups on startup and reuse the same objects. Each
	 * progress monitor needs its own group objects, because they store some of
	 * their own data on the object (and having two different progress monitors
	 * watching the same things is not exactly standard behaviour).
	 */
	data[0] = iris_progress_monitor_add_group (progress_monitor,
	                                           "Red Team", "Red Team");
	data[1] = iris_progress_monitor_add_group (progress_monitor,
	                                           "Blue Team", "Blue Team");
	data[2] = iris_progress_monitor_add_group (progress_monitor,
	                                           "Chartreuse Team", "Green Teams");
	data[3] = iris_progress_monitor_add_group (progress_monitor,
	                                           "Myrtle Team", "Green Teams");

	g_object_set_data (G_OBJECT (progress_monitor), "groups", data);
}

static void
create_progress_monitors (void)
{
	GtkWidget *dialog, *info_bar,
	          *vbox;

	g_return_if_fail (demo_window != NULL);

	/* The dialog */
	dialog = iris_progress_dialog_new (GTK_WINDOW (demo_window));
	iris_progress_dialog_set_title (IRIS_PROGRESS_DIALOG (dialog),
	                                "%s - Sheep counter");
	iris_progress_monitor_set_permanent_mode (IRIS_PROGRESS_MONITOR (dialog),
	                                          TRUE);
	monitor_list = g_list_prepend (monitor_list, dialog);
	create_groups (IRIS_PROGRESS_MONITOR (dialog));

	/* Delete is triggered if the user presses 'esc' in the progress dialog */
	g_signal_connect (dialog,
	                  "delete-event",
	                  G_CALLBACK (gtk_widget_hide_on_delete),
	                  NULL);

	/* The info bar */
	info_bar = iris_progress_info_bar_new ();

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
	create_groups (IRIS_PROGRESS_MONITOR (info_bar));
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

	/* Free things, hope that goes okay :) */
	for (node=monitor_list; node; node=node->next) {
		IrisProgressGroup **groups;
		gint                i;

		g_assert_cmpint (G_OBJECT (node->data)->ref_count, ==, 1);

		groups = g_object_get_data (G_OBJECT (node->data), "groups");
		for (i=0; i<4; i++)
			/* This may not actually free the group because we don't stop the
			 * running processes; they will have a reference to the group too
			 */
			iris_progress_group_unref (groups[i]);
		g_free (groups);

		gtk_widget_destroy (GTK_WIDGET (node->data));
	}

	return 0;
}
