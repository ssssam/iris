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

#include "iris-progress-monitor.h"
#include "iris-progress-monitor-private.h"
#include "iris-progress-dialog.h"
#include "iris-progress-dialog-private.h"

/**
 * SECTION:iris-progress-dialog
 * @title: IrisProgressDialog
 * @short_description: Dialog showing progress through tasks or processes.
 *
 * #IrisProgressDialog 
 * FIXME: Write
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
static void     iris_progress_dialog_update_watch    (IrisProgressMonitor *progress_monitor,
                                                      IrisProgressWatch   *watch);
static void     iris_progress_dialog_watch_stopped   (IrisProgressMonitor *progress_monitor);

static gboolean iris_progress_dialog_is_watching_process (IrisProgressMonitor *progress_monitor,
                                                          IrisProcess         *process);

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

	for (node=priv->watch_list; node; node=node->next)
		_iris_progress_watch_free (node->data);

	G_OBJECT_CLASS(iris_progress_dialog_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_watch           = iris_progress_dialog_add_watch;
	interface->update_watch        = iris_progress_dialog_update_watch;
	interface->watch_stopped       = iris_progress_dialog_watch_stopped;
	interface->is_watching_process = iris_progress_dialog_is_watching_process;
	interface->set_title           = iris_progress_dialog_set_title;
	interface->set_close_delay     = iris_progress_dialog_set_close_delay;
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
find_watch_by_process (gconstpointer a,
                       gconstpointer b)
{
	const IrisProgressWatch *watch = a;

	if (watch->process == b)
		return 0;
	return 1;
}

static void
iris_progress_dialog_add_watch (IrisProgressMonitor *progress_monitor,
                                IrisProgressWatch   *watch)
{
	IrisProgressDialog        *progress_dialog;
	IrisProgressDialogPrivate *priv;
	GtkDialog                 *dialog;

	GtkWidget *vbox_outer,
	          *title,
	          *hbox, *vbox_inner,
	          *progress_label, *progress_bar;
	gchar     *title_formatted;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = IRIS_PROGRESS_DIALOG (progress_monitor);
	priv = progress_dialog->priv;
	dialog = GTK_DIALOG (progress_dialog);

	priv->watch_list = g_list_append (priv->watch_list, watch);

	vbox_outer = gtk_vbox_new (FALSE, 4);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_outer), 4);

	title_formatted = g_strdup_printf ("<b>%s</b>", watch->title);
	title = gtk_label_new (title_formatted);
	gtk_label_set_use_markup (GTK_LABEL (title), TRUE);
	gtk_misc_set_alignment (GTK_MISC (title), 0.0, 0.5);
	g_free (title_formatted);

	hbox = gtk_hbox_new(FALSE, 0);
	vbox_inner = gtk_vbox_new(FALSE, 0);

	progress_bar = gtk_progress_bar_new ();
	progress_label = gtk_label_new (_("Preparing"));
	gtk_misc_set_alignment (GTK_MISC (progress_label), 0.0, 0.5);

	gtk_box_pack_start (GTK_BOX(vbox_inner), progress_bar, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX(vbox_inner), progress_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(hbox),       vbox_inner, TRUE, TRUE, 18);
	gtk_box_pack_start (GTK_BOX(vbox_outer), title, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX(vbox_outer), hbox, FALSE, FALSE, 0);
	gtk_widget_show_all(vbox_outer);

	gtk_container_add (GTK_CONTAINER (dialog->vbox), vbox_outer);

	watch->user_data = progress_bar;
	watch->user_data2 = progress_label;
}

static gboolean
iris_progress_dialog_is_watching_process (IrisProgressMonitor *progress_monitor,
                                          IrisProcess         *process)
{
	IrisProgressDialogPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor), FALSE);

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	return (g_list_find_custom (priv->watch_list, process,
	                            find_watch_by_process)     != NULL);
}

static void
iris_progress_dialog_update_watch (IrisProgressMonitor *progress_monitor,
                                   IrisProgressWatch   *watch)
{
	char  progress_text[256];
	IrisProgressDialog        *dialog;
	GtkWidget                 *progress_bar, *progress_label;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	dialog = IRIS_PROGRESS_DIALOG (progress_monitor);

	progress_bar = GTK_WIDGET (watch->user_data);
	progress_label = GTK_WIDGET (watch->user_data2);

	if (watch->complete)
		g_snprintf (progress_text, 255, _("Complete"));
	else {
		if (watch->cancelled)
			g_snprintf (progress_text, 255, _("Cancelled"));
		else
			g_snprintf (progress_text, 255, _("%i items of %i"), 
			            watch->processed_items,
			            watch->total_items);
	}

	gtk_label_set_text (GTK_LABEL (progress_label), progress_text);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
	                               watch->fraction);
};

static gboolean
_delayed_close (gpointer data)
{
	GtkWidget *dialog = GTK_WIDGET (data);

	gtk_widget_destroy (dialog);

	return FALSE;
}

/* When a watch completes/is cancelled, we check to see if any are still running
 * and if they are all done, we self destruct. */
void
iris_progress_dialog_watch_stopped (IrisProgressMonitor *progress_monitor)
{
	IrisProgressDialogPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	/* If any watches are still running, keep working .. */
	if (!_iris_progress_monitor_watch_list_finished (priv->watch_list))
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
		/* We're done - the button must a close button now. */

		if (priv->destroy_timer_id != 0)
			g_source_remove (priv->destroy_timer_id);

		_delayed_close (progress_dialog);
	} else
		_iris_progress_monitor_cancel (IRIS_PROGRESS_MONITOR (dialog),
		                               progress_dialog->priv->watch_list);
}
