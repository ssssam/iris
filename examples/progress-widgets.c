/* process-widgets.c
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

/* progress-widgets: demonstrates IrisProgressDialog and IrisProgressInfoBar. */

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gthread.h>

#include <iris/iris.h>
#include <iris/iris-gtk.h>

GtkWidget *demo_window = NULL;

/* Global list of IrisProgressMonitor widgets .. */
GList *monitors = NULL;

static void
remove_link (GtkWidget *caller,
             gpointer   user_data) {
	monitors = g_list_remove (monitors, user_data);
}

static void
create_progress_monitors () {
	GtkWidget *dialog, *info_bar,
	          *vbox;

	g_return_if_fail (demo_window != NULL);

	dialog = iris_progress_dialog_new ("Demo progress dialog",
	                                   GTK_WINDOW (demo_window));
	gtk_widget_show (dialog);
	iris_progress_monitor_set_close_delay (IRIS_PROGRESS_MONITOR (dialog),
	                                       500);
	g_signal_connect (dialog, "destroy", G_CALLBACK (remove_link), dialog);
	monitors = g_list_prepend (monitors, dialog);

	info_bar = iris_progress_info_bar_new ("Counting some sheep");
	vbox = gtk_dialog_get_content_area (GTK_DIALOG (demo_window));
	gtk_box_pack_end (GTK_BOX (vbox), info_bar, TRUE, FALSE, 0);
	gtk_widget_show (info_bar);
	iris_progress_monitor_set_close_delay (IRIS_PROGRESS_MONITOR (info_bar),
	                                       500);
	g_signal_connect (info_bar, "destroy", G_CALLBACK (remove_link), info_bar);
	monitors = g_list_prepend (monitors, info_bar);
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
	const gint   count = GPOINTER_TO_INT (user_data);

	process = iris_process_new_with_func (count_sheep_func, NULL, NULL);

	/* We add to the existing progress monitors if they exist, or create some
	 * more if they don't. */
	if (monitors == NULL)
		create_progress_monitors ();

	for (node=monitors; node; node=node->next) {
		IrisProgressMonitor *widget = IRIS_PROGRESS_MONITOR (node->data);
		iris_progress_monitor_watch_process (widget, process,
		                                     IRIS_PROGRESS_MONITOR_ITEMS);
	}

	for (i=0; i<count; i++)
		iris_process_enqueue (process, iris_message_new (0));
	iris_process_no_more_work (process);

	iris_process_run (process);
}

static GtkWidget *
create_demo_dialog (void)
{
	GtkWidget *vbox,
	          *triggers_box,
	          *button;

	demo_window = gtk_dialog_new_with_buttons
	                ("Iris progress widgets demo", NULL, 0,
	                 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
	                 NULL);

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

	gtk_box_pack_start (GTK_BOX (vbox), triggers_box, TRUE, TRUE, 4);
}

gint
main (gint argc, char *argv[])
{
	g_thread_init (NULL);
	gtk_init (&argc, &argv);

	create_demo_dialog ();
	g_signal_connect (demo_window, "response", gtk_main_quit, NULL);
	gtk_widget_show_all (demo_window);

	gtk_main ();
}
