/* iris-progress-dialog.c
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

#include <glib/gi18n.h>

#include "iris-arbiter.h"
#include "iris-gmainscheduler.h"
#include "iris-progress-monitor.h"
#include "iris-progress-monitor-private.h"
#include "iris-progress-dialog.h"
#include "iris-progress-dialog-private.h"

/**
 * SECTION:iris-progress-dialog
 * @title: IrisProgressDialog
 * @short_description: Dialog showing progress of tasks and processes.
 * @see_also: #IrisProgressInfoBar
 * @include: iris/iris-gtk.h
 *
 * #IrisProgressDialog creates a #GtkDialog which shows the status of various
 * #IrisProcess and #IrisTask objects, in a separate window to any application
 * windows. Use the #IrisProgressMonitor interface to control it.
 */

static void     iris_progress_dialog_class_init      (IrisProgressDialogClass *progress_dialog_class);
static void     iris_progress_dialog_init            (IrisProgressDialog *progress_dialog);
static void     iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface);

static GObject *iris_progress_dialog_constructor     (GType type,
                                                      guint n_construct_properties,
                                                      GObjectConstructParam *construct_params);
static void     iris_progress_dialog_finalize        (GObject *object);

static void     iris_progress_dialog_add_watch       (IrisProgressMonitor *progress_monitor,
                                                      IrisProgressWatch   *watch);
static void     iris_progress_dialog_handle_message  (IrisProgressMonitor *progress_monitor,
                                                      IrisProgressWatch   *watch,
                                                      IrisMessage         *message);

static gboolean iris_progress_dialog_is_watching_task (IrisProgressMonitor *progress_monitor,
                                                       IrisTask            *task);

static void     iris_progress_dialog_set_title       (IrisProgressMonitor *progress_monitor,
                                                      const gchar         *title);
static void     iris_progress_dialog_set_close_delay (IrisProgressMonitor *progress_monitor,
                                                      gint                 seconds);

static void     iris_progress_dialog_response        (GtkDialog           *dialog, 
                                                      int                  response_id,
                                                      gpointer             user_data);
 
G_DEFINE_TYPE_WITH_CODE (IrisProgressDialog, iris_progress_dialog, GTK_TYPE_DIALOG,
                         G_IMPLEMENT_INTERFACE (IRIS_TYPE_PROGRESS_MONITOR,
                                                iris_progress_monitor_interface_init))

static void
iris_progress_dialog_class_init (IrisProgressDialogClass *progress_dialog_class)
{
	GtkDialogClass *dialog_class = GTK_DIALOG_CLASS(progress_dialog_class);
	GObjectClass   *object_class = G_OBJECT_CLASS  (dialog_class);

	object_class->constructor = iris_progress_dialog_constructor;
	object_class->finalize    = iris_progress_dialog_finalize;

	iris_progress_dialog_parent_class = g_type_class_peek_parent (dialog_class);

	g_type_class_add_private (object_class, sizeof(IrisProgressDialogPrivate));
}

static void
iris_progress_dialog_init (IrisProgressDialog *progress_dialog)
{
	IrisProgressDialogPrivate *priv;

	priv = IRIS_PROGRESS_DIALOG_GET_PRIVATE (progress_dialog);

	progress_dialog->priv = priv;

	/* Use a gmainscheduler, so all our message processing happens in the
	 * main loop thread.
	 */
	priv->scheduler = iris_gmainscheduler_new (NULL);

	priv->watch_list = NULL;

	priv->completed = FALSE;

	priv->close_delay = 500;
}

static void
iris_progress_dialog_finalize (GObject *object)
{
	GList *node;
	IrisProgressDialog        *dialog = IRIS_PROGRESS_DIALOG (object);
	IrisProgressDialogPrivate *priv   = dialog->priv;

	for (node=priv->watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		iris_port_set_receiver (watch->port, NULL);

		/* Receiver must be freed, or we will get messages after we have
		 * been freed .. */
		g_warn_if_fail (G_OBJECT (watch->receiver)->ref_count == 1);

		g_object_unref (watch->receiver);

		_iris_progress_watch_free (node->data);
	}

	G_OBJECT_CLASS(iris_progress_dialog_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_watch        = iris_progress_dialog_add_watch;
	interface->handle_message   = iris_progress_dialog_handle_message;
	interface->is_watching_task = iris_progress_dialog_is_watching_task;
	interface->set_title        = iris_progress_dialog_set_title;
	interface->set_close_delay  = iris_progress_dialog_set_close_delay;
}

static GObject *
iris_progress_dialog_constructor (GType type,
                                  guint n_construct_properties,
                                  GObjectConstructParam *construct_params)
{
	GObject *object = ((GObjectClass *) iris_progress_dialog_parent_class)->constructor
	                    (type, n_construct_properties, construct_params);
	GtkDialog *dialog = GTK_DIALOG (object);

	IrisProgressDialog *progress_dialog = IRIS_PROGRESS_DIALOG (dialog);
	IrisProgressDialogPrivate *priv = progress_dialog->priv;

	/* FIXME: dialog should not take focus, unless explicitly clicked on. */

	gtk_dialog_set_has_separator (dialog, FALSE);
	priv->button = gtk_dialog_add_button (dialog, GTK_STOCK_CANCEL,
	                                      GTK_RESPONSE_CANCEL);

	gtk_container_set_border_width (GTK_CONTAINER (dialog->vbox), 8);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

	g_signal_connect_swapped (dialog, "response",
	                          G_CALLBACK (iris_progress_dialog_response),
	                          object);

	return object;
}


static gint
find_watch_by_task (gconstpointer a,
                    gconstpointer b)
{
	const IrisProgressWatch *watch = a;

	if (watch->task == IRIS_TASK (b))
		return 0;
	return 1;
}

static void
iris_progress_dialog_add_watch (IrisProgressMonitor *progress_monitor,
                                IrisProgressWatch   *watch)
{
	IrisProgressDialog        *progress_dialog;
	IrisProgressDialogPrivate *priv;

	GtkDialog *dialog;
	GtkWidget *vbox_outer,
	          *title,
	          *hbox, *vbox_inner,
	          *progress_label, *progress_bar;
	gchar     *title_formatted;


	progress_dialog = IRIS_PROGRESS_DIALOG (progress_monitor);
	priv = progress_dialog->priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	dialog = GTK_DIALOG (progress_dialog);

	/* Set up receiver. Messages are passed to the superclass first, which
	 * does prelimary processing before we take them. */

	watch->receiver = iris_arbiter_receive
	                    (priv->scheduler, watch->port, 
	                     _iris_progress_monitor_handle_message,
	                     watch, NULL);

	priv->watch_list = g_list_append (priv->watch_list, watch);

	/* Set up UI */

	vbox_outer = gtk_vbox_new (FALSE, 4);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_outer), 4);

	if (watch->title != NULL) {
		title_formatted = g_strdup_printf ("<b>%s</b>", watch->title);
		title = gtk_label_new (title_formatted);
		gtk_label_set_use_markup (GTK_LABEL (title), TRUE);
		gtk_misc_set_alignment (GTK_MISC (title), 0.0, 0.5);
		g_free (title_formatted);
	}

	hbox = gtk_hbox_new(FALSE, 0);
	vbox_inner = gtk_vbox_new(FALSE, 0);

	progress_bar = gtk_progress_bar_new ();
	progress_label = gtk_label_new (_("Preparing"));
	gtk_misc_set_alignment (GTK_MISC (progress_label), 0.0, 0.5);

	gtk_box_pack_start (GTK_BOX(vbox_inner), progress_bar, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX(vbox_inner), progress_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(hbox),       vbox_inner, TRUE, TRUE, 18);

	if (watch->title != NULL)
		gtk_box_pack_start (GTK_BOX(vbox_outer), title, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(vbox_outer), hbox, FALSE, FALSE, 0);
	gtk_widget_show_all(vbox_outer);

	gtk_container_add (GTK_CONTAINER (dialog->vbox), vbox_outer);

	watch->user_data = progress_bar;
	watch->user_data2 = progress_label;
}

static gboolean
iris_progress_dialog_is_watching_task (IrisProgressMonitor *progress_monitor,
                                       IrisTask            *task)
{
	IrisProgressDialogPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor), FALSE);

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	return (g_list_find_custom (priv->watch_list, task,
	                            find_watch_by_task)     != NULL);
}


/**************************************************************************
 *                        Message processing                              *
 *************************************************************************/

static gboolean
_delayed_close (gpointer data)
{
	GtkWidget *dialog = GTK_WIDGET (data);

	gtk_widget_destroy (dialog);

	return FALSE;
}

/* Cancelled or complete */
static void
handle_stopped (IrisProgressMonitor *progress_monitor,
                IrisProgressWatch   *watch,
                IrisMessage         *message)
{
	IrisProgressDialogPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	/* If any watches are still running, keep working .. */
	if (!_iris_progress_monitor_watch_list_finished (priv->watch_list))
		return;

	/* Check if we've already stopped (this can happen when two processes end
	 * at the same time)
	 */
	if (priv->destroy_timer_id != 0)
		return;

	/* We can delay the close to give the display time to update, and also so
	 * we can disable it for eg. tests
	 */
	if (priv->close_delay == 0)
		_delayed_close (progress_monitor);
	else {
		/* Make the 'cancel' button into a 'close' button */
		gtk_button_set_label (GTK_BUTTON (priv->button),
		                      _("Close"));
		priv->completed = TRUE;

		if (priv->close_delay > 0)
			priv->destroy_timer_id = g_timeout_add (priv->close_delay,
			                                        _delayed_close,
			                                        progress_monitor);
		else
			priv->destroy_timer_id = 0;
	}
}

static void
handle_update (IrisProgressMonitor *progress_monitor,
               IrisProgressWatch   *watch,
               IrisMessage         *message)
{
	char                 progress_text[256];
	IrisProgressDialog  *dialog;
	GtkWidget           *progress_bar, *progress_label;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	dialog = IRIS_PROGRESS_DIALOG (progress_monitor);

	progress_bar = GTK_WIDGET (watch->user_data);
	progress_label = GTK_WIDGET (watch->user_data2);

	_iris_progress_monitor_format_watch (progress_monitor, watch,
	                                     progress_text);

	gtk_label_set_text (GTK_LABEL (progress_label), progress_text);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
	                               watch->fraction);
}

static void
iris_progress_dialog_handle_message (IrisProgressMonitor *progress_monitor,
                                     IrisProgressWatch   *watch,
                                     IrisMessage         *message)
{
	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	switch (message->what) {
		case IRIS_PROGRESS_MESSAGE_COMPLETE:
		case IRIS_PROGRESS_MESSAGE_CANCELLED:
			handle_stopped (progress_monitor, watch, message);
			break;
		case IRIS_PROGRESS_MESSAGE_FRACTION:
		case IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS:
		case IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS:
			handle_update (progress_monitor, watch, message);
			break;
		default:
			g_warn_if_reached ();
	}
};


GtkWidget *
iris_progress_dialog_new (const gchar *title,
                          GtkWindow   *parent)
{
	GtkWidget *progress_dialog = g_object_new (IRIS_TYPE_PROGRESS_DIALOG, NULL);

	iris_progress_monitor_set_title (IRIS_PROGRESS_MONITOR (progress_dialog),
	                                 title);

	gtk_window_set_transient_for (GTK_WINDOW (progress_dialog), parent);

	return progress_dialog;
}


void
iris_progress_dialog_set_title (IrisProgressMonitor *progress_monitor,
                                const gchar         *title)
{
	IrisProgressDialog *progress_dialog;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = IRIS_PROGRESS_DIALOG (progress_monitor);

	gtk_window_set_title (GTK_WINDOW (progress_dialog), title);
}

void
iris_progress_dialog_set_close_delay (IrisProgressMonitor *progress_monitor,
                                      gint                 milliseconds)
{
	IrisProgressDialog *progress_dialog;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = IRIS_PROGRESS_DIALOG (progress_monitor);

	progress_dialog->priv->close_delay = milliseconds;
}


/* Handler for cancel button */
static void
iris_progress_dialog_response (GtkDialog *dialog,
                               int        response_id,
                               gpointer   user_data)
{
	IrisProgressDialog *progress_dialog = IRIS_PROGRESS_DIALOG (dialog);
	IrisProgressDialogPrivate *priv = progress_dialog->priv;

	if (priv->completed) {
		/* We're done - the button must be a close button now. */

		if (priv->destroy_timer_id != 0)
			g_source_remove (priv->destroy_timer_id);

		_delayed_close (progress_dialog);
	} else
		_iris_progress_monitor_cancel (IRIS_PROGRESS_MONITOR (dialog),
		                               progress_dialog->priv->watch_list);
}
