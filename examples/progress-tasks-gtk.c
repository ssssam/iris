/* progress-tasks.c
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

/* progress-tasks: demonstrates using IrisProgressMonitor for something other
 *                 than an IrisProcess. */

#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <glib/gthread.h>

#include <iris.h>
#include <iris-gtk.h>

GtkWidget *demo_window = NULL,
          *progress_widget = NULL,
          *title_entry = NULL;

GtkWidget *show_info_bar = NULL;

IrisProgressGroup *watch_group = NULL;

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
		/* The 'work' of this task is just sleeping, of course */
		g_usleep (50000);

		if (iris_task_is_cancelled (task)) {
			cancelled = TRUE;
			break;
		}

		/* Every so often we send a progress message! */
		status_message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_FRACTION,
		                                        G_TYPE_FLOAT,
		                                        (float)i/(float)count);
		iris_port_post (watch_port, status_message);
	}

	if (cancelled) {
		status_message = iris_message_new (IRIS_PROGRESS_MESSAGE_CANCELLED);
		iris_port_post (watch_port, status_message);
	} else {
		/* Make sure the 100% mark is reached, it looks strange for a watch to
		 * disappear before it reaches 100%
		 */
		status_message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_FRACTION,
		                                        G_TYPE_FLOAT, 1.0);
		iris_port_post (watch_port, status_message);

		status_message = iris_message_new (IRIS_PROGRESS_MESSAGE_COMPLETE);
		iris_port_post (watch_port, status_message);
	}

	g_object_unref (watch_port);
}

static void
trigger_task (GtkButton *trigger,
              gpointer   user_data)
{
	IrisTask *task;
	IrisPort *watch_port;

	task = iris_task_new (thinking_task_func, user_data, NULL);

	if (watch_group == NULL)
		watch_group = iris_progress_monitor_add_group
		                 (IRIS_PROGRESS_MONITOR (progress_widget),
		                  "Thinking about things",
		                  NULL);

	watch_port = iris_progress_monitor_add_watch
	               (IRIS_PROGRESS_MONITOR (progress_widget),
	                task,
	                "Thinking",
	                watch_group);
	g_object_ref (watch_port);
	g_object_set_data (G_OBJECT (task), "watch-port", watch_port);

	iris_task_run (task);
}

/* Change the type of progress monitor at any time. If there are some tasks
 * running they don't get displayed in the new progress monitor, because we
 * can't send them a new watch port. It would be possible to achieve this
 * fairly easily but it's basically a useless feature to support.
 */
static void
change_progress_monitor_cb (GObject    *toggle_button,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
	GtkWidget *vbox;

	g_return_if_fail (demo_window != NULL);

	if (progress_widget != NULL) {
		if (watch_group != NULL) {
			iris_progress_group_unref (watch_group);
			watch_group = NULL;
		}

		/* Get old progress monitor to destroy when its watches complete */
		iris_progress_monitor_set_permanent_mode
		  (IRIS_PROGRESS_MONITOR (progress_widget), FALSE);
		g_signal_connect (progress_widget, "finished", G_CALLBACK (gtk_widget_destroy), NULL);
	}

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle_button))) {
		progress_widget = gtk_iris_progress_info_bar_new ("Contemplating ...");
		vbox = gtk_dialog_get_content_area (GTK_DIALOG (demo_window));
		gtk_box_pack_end (GTK_BOX (vbox), progress_widget, FALSE, FALSE, 0);
	}
	else
		progress_widget = gtk_iris_progress_dialog_new (GTK_WINDOW (demo_window));

	iris_progress_monitor_set_permanent_mode
	  (IRIS_PROGRESS_MONITOR (progress_widget), TRUE);
}



static void
create_demo_dialog (void)
{
	GtkWidget *vbox,
	          *button;

	demo_window = gtk_dialog_new_with_buttons
	                ("Iris progress widgets demo", NULL, 0,
	                 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
	                 NULL);
	gtk_window_set_default_size (GTK_WINDOW (demo_window), 480, 240);

	vbox = gtk_dialog_get_content_area (GTK_DIALOG (demo_window));

	button = gtk_button_new_with_label ("Think about things");
	g_signal_connect (button, "clicked", G_CALLBACK (trigger_task),
	                  GINT_TO_POINTER (100));

	show_info_bar = gtk_check_button_new_with_label ("Show as info bar");
	g_signal_connect (show_info_bar,
	                  "notify::active",
	                  G_CALLBACK (change_progress_monitor_cb),
	                  NULL);

	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, TRUE, 8);
	gtk_box_pack_start (GTK_BOX (vbox), show_info_bar, FALSE, TRUE, 8);
}

gint
main (gint argc, char *argv[])
{
	g_thread_init (NULL);
	gtk_init (&argc, &argv);

	create_demo_dialog ();

	g_signal_connect (demo_window, "response", gtk_main_quit, NULL);
	gtk_widget_show_all (demo_window);

	/* Create initial progress widget */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (show_info_bar), TRUE);

	gtk_main ();

	gtk_widget_destroy (progress_widget);

	return 0;
}
