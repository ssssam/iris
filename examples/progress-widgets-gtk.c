/* progress-widgets.c
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

/* progress-widgets: demonstrates GtkIrisProgressDialog and GtkIrisProgressInfoBar. */

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gthread.h>

#include <iris.h>
#include <iris-gtk.h>

GtkWidget *demo_window = NULL,
          *title_entry = NULL;

GList *monitor_list = NULL,
      *process_list = NULL;


static void
remove_process_from_list (IrisTask *task,
                          gpointer  user_data)
{
	/* Called from callback/errback of IrisProcess in glib main loop */
	g_warn_if_fail (task == user_data);

	process_list = g_list_remove (process_list, user_data);
}

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
	gint         i;
	GList       *node;
	gint         count = GPOINTER_TO_INT (user_data);

	process = iris_process_new_with_func (count_sheep_func, NULL, NULL);
	iris_process_set_title (process, gtk_entry_get_text (GTK_ENTRY (title_entry)));

	iris_task_set_main_context (IRIS_TASK (process),
	                            g_main_context_default ());
	iris_task_add_both (IRIS_TASK (process),
	                    remove_process_from_list,
	                    remove_process_from_list,
	                    process,
	                    NULL);

	if (count == -1)
		iris_task_set_progress_mode (IRIS_TASK (process),
		                             IRIS_PROGRESS_ACTIVITY_ONLY);

	process_list = g_list_prepend (process_list, process);

	for (node=monitor_list; node; node=node->next) {
		IrisProgressMonitor *widget = IRIS_PROGRESS_MONITOR (node->data);

		iris_progress_monitor_watch_process (widget, process, NULL);
	}

	if (count == -1)
		count = g_random_int_range (5, 5000);
	for (i=0; i<count; i++)
		iris_process_enqueue (process, iris_message_new (0));
	iris_process_no_more_work (process);

	iris_process_run (process);
}

static void
title_entry_notify (GObject    *title_entry_object,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
	GList       *node;
	IrisProcess *process;
	const char  *title;

	title = gtk_entry_get_text (GTK_ENTRY (title_entry_object));

	for (node=process_list; node; node=node->next) {
		process = IRIS_PROCESS (node->data);
		iris_process_set_title (process, title);
	}
}

static void
create_demo_dialog (void)
{
	GtkWidget *vbox,
	          *hbox,
	          *triggers_box,
	          *button,
	          *label;

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

	triggers_box = gtk_hbutton_box_new ();
	gtk_button_box_set_spacing (triggers_box, 8);

	button = gtk_button_new_with_label ("Count 10 sheep");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (10));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label ("Count 100 sheep");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (100));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label ("Count 1000 sheep");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (1000));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	button = gtk_button_new_with_label ("Find lost sheep");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_process),
	                  GINT_TO_POINTER (-1));
	gtk_box_pack_start (GTK_BOX (triggers_box), button, TRUE, TRUE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	label = gtk_label_new ("Process title: ");

	title_entry = gtk_entry_new ();
	gtk_widget_set_size_request (title_entry, 200, -1);
	g_signal_connect (title_entry, "notify::text", G_CALLBACK (title_entry_notify), NULL);
	gtk_entry_set_text (GTK_ENTRY (title_entry), "Shepherding");

	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), title_entry, FALSE, TRUE, 4);

	gtk_box_pack_start (GTK_BOX (vbox), triggers_box, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 4);
}

static void
create_progress_monitors (void)
{
	GtkWidget *dialog, *info_bar,
	          *vbox;

	g_return_if_fail (demo_window != NULL);

	/* The dialog */
	dialog = gtk_iris_progress_dialog_new (GTK_WINDOW (demo_window));
	gtk_iris_progress_dialog_set_title (GTK_IRIS_PROGRESS_DIALOG (dialog),
	                                "%s - Sheep counter");
	iris_progress_monitor_set_permanent_mode (IRIS_PROGRESS_MONITOR (dialog),
	                                          TRUE);
	monitor_list = g_list_prepend (monitor_list, dialog);

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
		g_assert_cmpint (G_OBJECT (node->data)->ref_count, ==, 1);
		gtk_widget_destroy (GTK_WIDGET (node->data));
	}

	return 0;
}
