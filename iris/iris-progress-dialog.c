/* iris-progress-dialog.c
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

static void     iris_progress_dialog_class_init           (IrisProgressDialogClass  *progress_dialog_class);
static void     iris_progress_dialog_init                 (IrisProgressDialog *progress_dialog);
static void     iris_progress_monitor_interface_init      (IrisProgressMonitorInterface *interface);

static GObject *iris_progress_dialog_constructor          (GType type,
                                                           guint n_construct_properties,
                                                           GObjectConstructParam *construct_params);
static void     iris_progress_dialog_finalize             (GObject *object);

static void     iris_progress_dialog_add_watch            (IrisProgressMonitor *progress_monitor,
                                                           IrisProgressWatch   *watch);
static void     iris_progress_dialog_handle_message       (IrisProgressMonitor *progress_monitor,
                                                           IrisProgressWatch   *watch,
                                                           IrisMessage         *message);

static gboolean iris_progress_dialog_is_watching_task     (IrisProgressMonitor *progress_monitor,
                                                           IrisTask            *task);

static void     format_watch_title                        (GtkWidget           *label,
                                                           const gchar         *title);
static void     update_title                              (IrisProgressDialog  *progress_dialog,
                                                           gchar               *progress_text_precalculated);

static void     iris_progress_dialog_set_permanent_mode   (IrisProgressMonitor *progress_monitor,
                                                           gboolean             enable);
static void     iris_progress_dialog_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                                           gint                 millisecpnds);

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

	priv->title_format = NULL;

	priv->permanent_mode = FALSE;

	priv->watch_hide_delay = 500;

	priv->in_finished = FALSE;
	priv->title_is_static = FALSE;

	/* A window with no title is never allowed! */
	update_title (IRIS_PROGRESS_DIALOG (progress_dialog), NULL);
}

static void
iris_progress_dialog_finalize (GObject *object)
{
	GList *node;
	IrisProgressDialog        *dialog = IRIS_PROGRESS_DIALOG (object);
	IrisProgressDialogPrivate *priv   = dialog->priv;

	/* Closing a progress monitor with active watches is not recommended but it
	 * should still work.
	 */
	for (node=priv->watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		if (watch->finish_timeout_id != 0)
			g_source_remove (watch->finish_timeout_id);

		/* Receiver must be freed, or we will get messages after we have
		 * been freed .. */
		iris_port_set_receiver (watch->port, NULL);

		g_warn_if_fail (G_OBJECT (watch->receiver)->ref_count == 1);

		g_object_unref (watch->receiver);

		_iris_progress_watch_free (node->data);
	}

	G_OBJECT_CLASS(iris_progress_dialog_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_watch            = iris_progress_dialog_add_watch;
	interface->handle_message       = iris_progress_dialog_handle_message;
	interface->is_watching_task     = iris_progress_dialog_is_watching_task;
	interface->set_permanent_mode   = iris_progress_dialog_set_permanent_mode;
	interface->set_watch_hide_delay = iris_progress_dialog_set_watch_hide_delay;
}

static GObject *
iris_progress_dialog_constructor (GType type,
                                  guint n_construct_properties,
                                  GObjectConstructParam *construct_params)
{
	GObject *object = ((GObjectClass *) iris_progress_dialog_parent_class)->constructor
	                    (type, n_construct_properties, construct_params);
	GtkDialog *dialog = GTK_DIALOG (object);

	/* FIXME: dialog should not take focus, unless explicitly clicked on. */

	gtk_dialog_set_has_separator (dialog, FALSE);

	gtk_container_set_border_width (GTK_CONTAINER (dialog->vbox), 8);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

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

	progress_dialog = IRIS_PROGRESS_DIALOG (progress_monitor);
	priv = progress_dialog->priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	dialog = GTK_DIALOG (progress_dialog);

	/* There is no technical reason not to add watches in the finish handler,
	 * but also no reason to want to do it. If you want to spawn a process when
	 * another finishes, that should be triggered by the process object and not
	 * the progress monitor.
	 */
	g_return_if_fail (!priv->in_finished);

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

	title = gtk_label_new ("");
	gtk_label_set_use_markup (GTK_LABEL (title), TRUE);
	gtk_misc_set_alignment (GTK_MISC (title), 0.0, 0.5);
	format_watch_title (title, watch->title);

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

	watch->container = vbox_outer;
	watch->title_label = title;
	watch->progress_bar = progress_bar;
	watch->progress_label = progress_label;
	watch->cancel_button = NULL;

	if (priv->permanent_mode == TRUE)
		/* Ensure we are visible; quicker just to call than to check */
		gtk_widget_show (GTK_WIDGET (dialog));
}

static gboolean
iris_progress_dialog_is_watching_task (IrisProgressMonitor *progress_monitor,
                                       IrisTask            *task)
{
	IrisProgressDialogPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor), FALSE);

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	return (_iris_progress_dialog_get_watch (IRIS_PROGRESS_DIALOG (progress_monitor),
	                                         task) != NULL);
}

static void
format_watch_title (GtkWidget   *label,
                    const gchar *title)
{
	gchar *title_formatted;
	
	if (title == NULL)
		gtk_label_set_text (GTK_LABEL (label), "");
	else {
		title_formatted = g_strdup_printf ("<b>%s</b>", title);
		gtk_label_set_markup (GTK_LABEL (label), title_formatted);
		g_free (title_formatted);
	}
}

/*static void
set_completed (IrisProgressDialog *progress_dialog,
               gboolean            completed)
{
	IrisProgressDialogPrivate *priv;

	priv = progress_dialog->priv;

	priv->completed = completed;

	if (completed)
		gtk_button_set_label (GTK_BUTTON (priv->button),
		                      _("Close"));
	else
		gtk_button_set_label (GTK_BUTTON (priv->button),
		                      _("Close"));
};*/

/* Private, for tests */
IrisProgressWatch *
_iris_progress_dialog_get_watch (IrisProgressDialog *progress_dialog,
                                 IrisTask           *task)
{
	IrisProgressDialogPrivate *priv;
	GList                     *node;

	g_return_val_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_dialog), NULL);

	priv = progress_dialog->priv;

	node = g_list_find_custom (priv->watch_list, task, find_watch_by_task);

	return node? node->data: NULL;
}


/**************************************************************************
 *                        Message processing                              *
 *************************************************************************/

/* No need to worry about locking here, remember these messages are processed in the main loop */

static void
update_title (IrisProgressDialog *progress_dialog,
              gchar              *progress_text_precalculated)
{
	IrisProgressDialogPrivate *priv;
	IrisProgressWatch         *head_watch;
	GString                   *string;
	char                       progress_text[256];

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_dialog));

	priv = progress_dialog->priv;

	if (priv->title_is_static) {
		gtk_window_set_title (GTK_WINDOW (progress_dialog),
		                      priv->title_format);
		return;
	}

	string = g_string_new (NULL);

	if (priv->watch_list == NULL)
		g_string_append (string, _("Progress Monitor"));
	else {
		head_watch = priv->watch_list->data;

		if (head_watch->title != NULL && head_watch->title[0] != '\0') {
			g_string_append (string, head_watch->title);
			g_string_append (string, " - ");
		}

		if (progress_text_precalculated == NULL) {
			_iris_progress_monitor_format_watch (IRIS_PROGRESS_MONITOR (progress_dialog),
			                                     head_watch,
			                                     progress_text);
			g_string_append (string, progress_text);
		}
		else
			g_string_append (string, progress_text_precalculated);
	}

	if (priv->title_format) {
		char *temp = g_string_free (string, FALSE);
		string = g_string_new (NULL);
		g_string_printf (string, priv->title_format, temp);
		g_free (temp);
	}

	gtk_window_set_title (GTK_WINDOW (progress_dialog),
	                      string->str);

	g_string_free (string, TRUE);
}

static void
dialog_finish (IrisProgressDialog *progress_dialog)
{
	IrisProgressDialogPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_dialog));

	priv = progress_dialog->priv;

	/* Emit IrisProgressMonitor::finished */
	_iris_progress_monitor_finished (IRIS_PROGRESS_MONITOR (progress_dialog));

	if (priv->permanent_mode) {
		/* Check the 'finished' handler didn't destroy the dialog */
		g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_dialog));

		gtk_widget_hide (GTK_WIDGET (progress_dialog));
		update_title (progress_dialog, NULL);
	}
}

/* Called by watch hide timeout */
static gboolean
watch_delayed_finish (gpointer data)
{
	IrisProgressWatch         *watch = data;
	IrisProgressDialog        *progress_dialog;
	IrisProgressDialogPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROGRESS_DIALOG (watch->monitor), FALSE);

	progress_dialog = IRIS_PROGRESS_DIALOG (watch->monitor);
	priv = progress_dialog->priv;

	gtk_widget_destroy (GTK_WIDGET (watch->container));

	/* Remove self from watch list */
	priv->watch_list = g_list_remove (priv->watch_list, watch);

	update_title (progress_dialog, NULL);

	/* If no watches left, hide dialog */
	if (priv->watch_list == NULL)
		dialog_finish (progress_dialog);

	return FALSE;
}

/* Cancelled or complete */
static void
handle_stopped (IrisProgressMonitor *progress_monitor,
                IrisProgressWatch   *watch)
{
	IrisProgressDialogPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	if (priv->watch_hide_delay == 0)
		watch_delayed_finish (watch);
	else if (priv->watch_hide_delay == -1);
		/* Never hide watch; for debugging purposes */
	else {
		watch->finish_timeout_id = g_timeout_add (priv->watch_hide_delay,
		                                          watch_delayed_finish,
		                                          watch);
	}
}

static void
handle_update (IrisProgressMonitor *progress_monitor,
               IrisProgressWatch   *watch)
{
	IrisProgressDialogPrivate *priv;
	char                       progress_text[256];
	GtkWidget                 *progress_bar, *progress_label;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	progress_bar = GTK_WIDGET (watch->progress_bar);
	progress_label = GTK_WIDGET (watch->progress_label);

	_iris_progress_monitor_format_watch (progress_monitor, watch,
	                                     progress_text);

	gtk_label_set_text (GTK_LABEL (progress_label), progress_text);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
	                               watch->fraction);

	if (priv->watch_list->data == watch)
		update_title (IRIS_PROGRESS_DIALOG (progress_monitor), progress_text);
}

static void
handle_title (IrisProgressMonitor *progress_monitor,
              IrisProgressWatch   *watch)
{
	IrisProgressDialogPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	format_watch_title (GTK_WIDGET (watch->title_label), watch->title);

	if (priv->watch_list->data == watch)
		update_title (IRIS_PROGRESS_DIALOG (progress_monitor), NULL);
}


static void
iris_progress_dialog_handle_message (IrisProgressMonitor *progress_monitor,
                                     IrisProgressWatch   *watch,
                                     IrisMessage         *message)
{
	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	/* Message has already been parsed by interface and 'watch' updated */

	switch (message->what) {
		case IRIS_PROGRESS_MESSAGE_COMPLETE:
		case IRIS_PROGRESS_MESSAGE_CANCELLED:
			handle_stopped (progress_monitor, watch);
			break;
		case IRIS_PROGRESS_MESSAGE_FRACTION:
		case IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS:
		case IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS:
			handle_update (progress_monitor, watch);
			break;
		case IRIS_PROGRESS_MESSAGE_TITLE:
			handle_title (progress_monitor, watch);
			break;
		default:
			g_warn_if_reached ();
	}
};


/**
 * iris_progress_dialog_new:
 * @parent: window to act as the parent of this dialog, or %NULL
 *
 * Creates a new #IrisProgressDialog. By default, the title of the dialog will
 * be the title and status of the uppermost process or process group being
 * watched, in the following format: "Checking Data - 46% complete". You may
 * set a different window title using iris_progress_dialog_set_title(); do not
 * use gtk_window_set_title() or your title will be overwritten by status
 * updates.
 *
 * Return value: a newly-created #IrisProgressDialog widget
 */
GtkWidget *
iris_progress_dialog_new (GtkWindow   *parent)
{
	GtkWidget *progress_dialog = g_object_new (IRIS_TYPE_PROGRESS_DIALOG, NULL);

	gtk_window_set_transient_for (GTK_WINDOW (progress_dialog), parent);

	return progress_dialog;
}


/**
 * iris_progress_dialog_set_title:
 * @progress_dialog: an #IrisProgressDialog
 * @title_format: string to set as the window title for @progress_dialog
 *
 * This function sets the window title of @progress_dialog. By default the
 * dialog title is the status of the first process or process group that is
 * being watched, and you may include this in @title_format using the marker
 * <literal>%%s</literal>. For example, the following call:
 * |[
 *   iris_progress_monitor_set_title (progress_monitor, "%s - My Application");
 * ]|
 * will result in a title such as
 * <literal>"Processing Data - 4 items of 50 - My Application"</literal>.
 *
 * If the current watch does not have a title, only its progress is displayed.
 * If there is currently nothing being watched, the title defaults to
 * <literal>"Progress Monitor"</literal>.
 *
 * <warning>
 * <para>
 * You cannot use gtk_window_set_title() to set the dialog's title, because the
 * title will be overwritten when the progress changes.
 * </para>
 * </warning>
 **/
void
iris_progress_dialog_set_title (IrisProgressDialog *progress_dialog,
                                const gchar        *title_format)
{
	IrisProgressDialogPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_dialog));

	priv = progress_dialog->priv;

	g_free (priv->title_format);

	if (title_format == NULL) {
		priv->title_format = NULL;
		priv->title_is_static = FALSE;
	}
	else {
		/* Search for '%s' in title_format to see if we need to keep it updated
		 * with progress info
		 */
		const gchar *c = title_format;
		priv->title_is_static = TRUE;
		while ((c = strchr (c, '%')) != NULL) {
			g_return_if_fail ((*c) != 0);

			if (*(c+1) == 's') {
				priv->title_is_static = FALSE;
				break;
			}
		}

		priv->title_format = g_strdup (title_format);
	}

	update_title (progress_dialog, NULL);
};

void
iris_progress_dialog_set_permanent_mode (IrisProgressMonitor *progress_monitor,
                                         gboolean             enable)
{
	IrisProgressDialog *progress_dialog;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = IRIS_PROGRESS_DIALOG (progress_monitor);

	progress_dialog->priv->permanent_mode = enable;
}

void
iris_progress_dialog_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                           gint                 milliseconds)
{
	IrisProgressDialog *progress_dialog;

	g_return_if_fail (IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = IRIS_PROGRESS_DIALOG (progress_monitor);

	progress_dialog->priv->watch_hide_delay = milliseconds;
}
