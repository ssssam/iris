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
#include "gtk-iris-progress-dialog.h"
#include "gtk-iris-progress-dialog-private.h"

/**
 * SECTION:gtk-iris-progress-dialog
 * @title: GtkIrisProgressDialog
 * @short_description: Dialog showing progress of tasks and processes.
 * @see_also: #GtkIrisProgressInfoBar
 * @include: iris/iris-gtk.h
 *
 * #GtkIrisProgressDialog creates a #GtkDialog which shows the status of various
 * #IrisProcess and #IrisTask objects, in a separate window to any application
 * windows. Use the #IrisProgressMonitor interface to control the dialog.
 *
 * The tasks being monitored will communicate by passing messages to the
 * #GtkIrisProgressDialog, which enables the Gtk+ work to be done entirely from
 * the GLib main loop thread. You may not call any of the #IrisProgressMonitor
 * interface methods outside of the GLib main loop thread, as with all portable
 * Gtk+ code.
 */

static void     gtk_iris_progress_dialog_class_init           (GtkIrisProgressDialogClass  *progress_dialog_class);
static void     gtk_iris_progress_dialog_init                 (GtkIrisProgressDialog *progress_dialog);
static void     iris_progress_monitor_interface_init          (IrisProgressMonitorInterface *interface);

static GObject *gtk_iris_progress_dialog_constructor          (GType type,
                                                               guint n_construct_properties,
                                                               GObjectConstructParam *construct_params);
static void     gtk_iris_progress_dialog_dispose              (GObject *object);
static void     gtk_iris_progress_dialog_finalize             (GObject *object);

static void     gtk_iris_progress_dialog_get_preferred_width  (GtkWidget *widget,
                                                               gint      *minimum_width,
                                                               gint      *natural_width);

static void     gtk_iris_progress_dialog_add_group              (IrisProgressMonitor *progress_monitor,
                                                                 IrisProgressGroup   *group);
static void     gtk_iris_progress_dialog_remove_group           (IrisProgressMonitor *progress_monitor,
                                                                 IrisProgressGroup   *group);
static void     gtk_iris_progress_dialog_add_watch              (IrisProgressMonitor *progress_monitor,
                                                                 IrisProgressWatch   *watch);
static void     gtk_iris_progress_dialog_remove_watch           (IrisProgressMonitor *progress_monitor,
                                                                 IrisProgressWatch   *watch);
static void     gtk_iris_progress_dialog_reorder_watch_in_group (IrisProgressMonitor *progress_monitor,
                                                                 IrisProgressWatch   *watch,
                                                                 gboolean             at_end);

static void     handle_update                                 (IrisProgressMonitor *progress_monitor,
                                                               IrisProgressWatch   *watch);
static void     gtk_iris_progress_dialog_handle_message       (IrisProgressMonitor *progress_monitor,
                                                               IrisProgressWatch   *watch,
                                                               IrisMessage         *message);

static gboolean gtk_iris_progress_dialog_is_watching_task     (IrisProgressMonitor *progress_monitor,
                                                               IrisTask            *task);

static void     format_watch_title                            (GtkWidget           *label,
                                                               const gchar         *title,
                                                               gboolean             grouped);
static void     update_dialog_title                           (GtkIrisProgressDialog  *progress_dialog,
                                                               gchar               *progress_text_precalculated);
static void     finish_dialog                                 (GtkIrisProgressDialog  *progress_dialog);

static void     gtk_iris_progress_dialog_set_permanent_mode   (IrisProgressMonitor *progress_monitor,
                                                               gboolean             enable);
static void     gtk_iris_progress_dialog_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                                               gint                 millisecpnds);

IrisProgressWatch *gtk_iris_progress_dialog_get_watch         (IrisProgressMonitor *progress_monitor,
                                                               IrisTask            *task);

G_DEFINE_TYPE_WITH_CODE (GtkIrisProgressDialog, gtk_iris_progress_dialog, GTK_TYPE_DIALOG,
                         G_IMPLEMENT_INTERFACE (IRIS_TYPE_PROGRESS_MONITOR,
                                                iris_progress_monitor_interface_init))

/* Spacing between dialog edge and watch/group widgets */
#define OUTER_PADDING 12

/* Group spacing (also for watches outside of groups) */
#define GROUP_V_SPACING 12

/* Layout of individual watch titles & progress bars */
#define WATCH_V_SPACING 2
#define WATCH_INDENT 18
#define WATCH_H_SPACING 4

static void
gtk_iris_progress_dialog_class_init (GtkIrisProgressDialogClass *progress_dialog_class)
{
	GObjectClass   *object_class = G_OBJECT_CLASS  (progress_dialog_class);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (progress_dialog_class);

	object_class->constructor = gtk_iris_progress_dialog_constructor;
	object_class->dispose     = gtk_iris_progress_dialog_dispose;
	object_class->finalize    = gtk_iris_progress_dialog_finalize;

	widget_class->get_preferred_width = gtk_iris_progress_dialog_get_preferred_width;

	g_type_class_add_private (object_class, sizeof(GtkIrisProgressDialogPrivate));
}

static void
gtk_iris_progress_dialog_init (GtkIrisProgressDialog *progress_dialog)
{
	GtkIrisProgressDialogPrivate *priv;

	priv = GTK_IRIS_PROGRESS_DIALOG_GET_PRIVATE (progress_dialog);

	progress_dialog->priv = priv;

	/* Use a gmainscheduler, so all our message processing happens in the
	 * main loop thread.
	 */
	priv->scheduler = iris_gmainscheduler_new (NULL);

	priv->watch_list = NULL;

	priv->box = gtk_dialog_get_content_area (GTK_DIALOG (progress_dialog));
	priv->title_format = NULL;
	priv->max_width = 0;

	priv->watch_hide_delay = 750;

	priv->permanent_mode = FALSE;

	priv->in_finished = FALSE;
	priv->title_is_static = FALSE;

	/* A window with no title is never allowed! */
	update_dialog_title (GTK_IRIS_PROGRESS_DIALOG (progress_dialog), NULL);
}

static void
gtk_iris_progress_dialog_dispose (GObject *object)
{
	GtkIrisProgressDialogPrivate *priv;

	priv = GTK_IRIS_PROGRESS_DIALOG (object)->priv;

	priv->in_destruction = TRUE;
	G_OBJECT_CLASS (gtk_iris_progress_dialog_parent_class)->dispose (object);
}

static void
gtk_iris_progress_dialog_finalize (GObject *object)
{
	GList *node;
	GtkIrisProgressDialog        *dialog = GTK_IRIS_PROGRESS_DIALOG (object);
	GtkIrisProgressDialogPrivate *priv   = dialog->priv;

	for (node=priv->watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		if (watch->finish_timeout_id != 0)
			g_source_remove (watch->finish_timeout_id);

		_iris_progress_watch_disconnect (node->data);
		_iris_progress_watch_free (node->data);
	}

	G_OBJECT_CLASS(gtk_iris_progress_dialog_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_group            = gtk_iris_progress_dialog_add_group;
	interface->add_watch            = gtk_iris_progress_dialog_add_watch;
	interface->handle_message       = gtk_iris_progress_dialog_handle_message;
	interface->is_watching_task     = gtk_iris_progress_dialog_is_watching_task;
	interface->set_permanent_mode   = gtk_iris_progress_dialog_set_permanent_mode;
	interface->set_watch_hide_delay = gtk_iris_progress_dialog_set_watch_hide_delay;

	/* Private */
	interface->remove_group           = gtk_iris_progress_dialog_remove_group;
	interface->remove_watch           = gtk_iris_progress_dialog_remove_watch;
	interface->reorder_watch_in_group = gtk_iris_progress_dialog_reorder_watch_in_group;
	interface->get_watch              = gtk_iris_progress_dialog_get_watch;
}

static GObject *
gtk_iris_progress_dialog_constructor (GType type,
                                      guint n_construct_properties,
                                      GObjectConstructParam *construct_params)
{
	GObject *object = ((GObjectClass *) gtk_iris_progress_dialog_parent_class)->constructor
	                    (type, n_construct_properties, construct_params);
	GtkDialog *dialog = GTK_DIALOG (object);

	/* FIXME: dialog should not take focus, unless explicitly clicked on. */

	/* Construction of outer dialog UI */

	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_deletable (GTK_WINDOW (dialog), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dialog), OUTER_PADDING);

	gtk_box_set_spacing (GTK_BOX (gtk_dialog_get_content_area (dialog)),
	                     GROUP_V_SPACING);

	return object;
}

/* Prevent dialog from shrinking width-ways when watches with long titles are removed */
static void
gtk_iris_progress_dialog_get_preferred_width (GtkWidget *widget,
                                              gint      *minimum_width,
                                              gint      *natural_width)
{
	GtkIrisProgressDialog        *dialog;
	GtkIrisProgressDialogPrivate *priv;
	GtkWidgetClass               *widget_class;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (widget));

	dialog = GTK_IRIS_PROGRESS_DIALOG (widget);
	priv = dialog->priv;

	widget_class = GTK_WIDGET_CLASS (gtk_iris_progress_dialog_parent_class);
	widget_class->get_preferred_width (widget, minimum_width, natural_width);

	/* It seems logical to only set the natural width, but in practice the
	 * dialog seems to always get its minimum width.
	 */
	if (*minimum_width > priv->max_width)
		priv->max_width = *minimum_width;
	else {
		*minimum_width = priv->max_width;
		*natural_width = priv->max_width;
	}
}


static GtkWidget *
new_cancel_button () {
	GtkWidget *button,
	          *image;

	image = gtk_image_new_from_stock (GTK_STOCK_CANCEL,
	                                  GTK_ICON_SIZE_BUTTON);
	button = gtk_button_new();
	gtk_container_add (GTK_CONTAINER (button), image);

	return button;
}

static void
group_cancel_clicked (GtkButton *cancel_button,
                      gpointer   user_data) {
	IrisProgressMonitor *progress_monitor;
	IrisProgressGroup   *group;

	progress_monitor = IRIS_PROGRESS_MONITOR (user_data);
	group = g_object_get_data (G_OBJECT (cancel_button), "group");

	gtk_widget_set_sensitive (GTK_WIDGET (cancel_button), FALSE);

	_iris_progress_monitor_cancel_group (progress_monitor, group);
}

static void
gtk_iris_progress_dialog_add_group (IrisProgressMonitor *progress_monitor,
                                    IrisProgressGroup   *group) {
	GtkWidget *toplevel_hbox, *toplevel_vbox,
	          *watch_vbox,
	          *label,
	          *cancel_button, *cancel_alignment;
	gchar     *title;

	/* Construction of group UI */
	toplevel_hbox = gtk_hbox_new (FALSE, WATCH_H_SPACING);
	toplevel_vbox = gtk_vbox_new (FALSE, WATCH_V_SPACING);

	title = g_strdup_printf ("<b>%s</b>", group->title);
	label = gtk_label_new ("");
	gtk_label_set_markup (GTK_LABEL (label), title);
	gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

	cancel_button = new_cancel_button (progress_monitor, group);
	cancel_alignment = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (cancel_alignment), cancel_button);

	g_object_set_data (G_OBJECT (cancel_button), "group", group);
	g_signal_connect (G_OBJECT (cancel_button),
	                  "clicked",
	                  G_CALLBACK (group_cancel_clicked),
	                  progress_monitor);

	watch_vbox = gtk_vbox_new (TRUE, WATCH_V_SPACING);

	gtk_box_pack_start (GTK_BOX (toplevel_vbox), label, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (toplevel_vbox), watch_vbox, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (toplevel_hbox), toplevel_vbox, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (toplevel_hbox), cancel_alignment, FALSE, FALSE, 0);

	g_object_ref_sink (G_OBJECT (toplevel_hbox));

	group->toplevel = toplevel_hbox;
	group->watch_box = watch_vbox;
	group->progress_bar = NULL;
	group->cancel_widget = cancel_button;

	/* Used to align the watch title labels */
	group->user_data1 = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
}

/* Called on group destruction */
static void
gtk_iris_progress_dialog_remove_group (IrisProgressMonitor *progress_monitor,
                                       IrisProgressGroup   *group)
{
	g_return_if_fail (GTK_IS_WIDGET (group->toplevel));
	g_warn_if_fail (G_OBJECT (group->toplevel)->ref_count == 1);

	gtk_widget_destroy (group->toplevel);
	g_object_unref (group->user_data1);
}

static void
show_group (GtkIrisProgressDialog *progress_dialog,
            IrisProgressGroup  *group)
{
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_dialog));
	g_return_if_fail (group->visible == FALSE);

	priv = progress_dialog->priv;

	gtk_box_pack_start (GTK_BOX (priv->box), group->toplevel, FALSE, TRUE, 0);
	/*gtk_widget_show_all (GTK_WIDGET (group->toplevel));*/
	gtk_widget_show_all (GTK_WIDGET (priv->box));

	group->visible = TRUE;
}

static void
hide_group (GtkIrisProgressDialog *progress_dialog,
            IrisProgressGroup  *group)
{
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_dialog));
	g_return_if_fail (group->visible == TRUE);

	priv = progress_dialog->priv;

	gtk_container_remove (GTK_CONTAINER (priv->box), group->toplevel);

	/* Reset completed watch counts */
	_iris_progress_group_reset (group);

	group->visible = FALSE;
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
watch_cancel_clicked (GtkButton *cancel_button,
                      gpointer   user_data) {
	IrisProgressMonitor *progress_monitor;
	IrisProgressWatch   *watch;

	progress_monitor = IRIS_PROGRESS_MONITOR (user_data);
	watch = g_object_get_data (G_OBJECT (cancel_button), "watch");

	gtk_widget_set_sensitive (GTK_WIDGET (cancel_button), FALSE);

	_iris_progress_monitor_cancel_watch (progress_monitor, watch);
}

static void
gtk_iris_progress_dialog_add_watch (IrisProgressMonitor *progress_monitor,
                                    IrisProgressWatch   *watch)
{
	GtkIrisProgressDialog        *progress_dialog;
	GtkIrisProgressDialogPrivate *priv;

	GtkWidget *vbox, *vbox2,
	          *hbox,
	          *indent,
	          *title_label,
	          *progress_bar,
	          *cancel_button;

	progress_dialog = GTK_IRIS_PROGRESS_DIALOG (progress_monitor);
	priv = progress_dialog->priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

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

	/* It's important to have this list in order, so the dialog can show the
	 * oldest watch's progress in its title bar.
	 */
	priv->watch_list = g_list_append (priv->watch_list, watch);

	/* Set up UI */

	title_label = gtk_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (title_label), 0.0, 0.5);
	format_watch_title (title_label, watch->title, watch->group != NULL);

	progress_bar = gtk_progress_bar_new ();
	/*gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar), _("Preparing"));*/

	if (watch->group == NULL) {
		/* Construction of stand-alone watch UI */
		vbox = gtk_vbox_new (FALSE, WATCH_H_SPACING);

		vbox2 = gtk_vbox_new(FALSE, 0);
		hbox = gtk_hbox_new (FALSE, WATCH_H_SPACING);

		indent = gtk_label_new ("");
		gtk_widget_set_size_request (indent, WATCH_INDENT, 0);

		cancel_button = new_cancel_button ();

		g_object_set_data (G_OBJECT (cancel_button), "watch", watch);
		g_signal_connect (G_OBJECT (cancel_button),
		                  "clicked",
		                  G_CALLBACK (watch_cancel_clicked),
		                  progress_monitor);

		gtk_box_pack_start (GTK_BOX(hbox), indent, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX(hbox), progress_bar, TRUE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX(hbox), cancel_button, FALSE, FALSE, 0);

		gtk_box_pack_start (GTK_BOX(vbox), title_label, FALSE, FALSE, WATCH_V_SPACING);
		gtk_box_pack_start (GTK_BOX(vbox), hbox, FALSE, FALSE, WATCH_V_SPACING);

		gtk_box_pack_start (GTK_BOX (priv->box), vbox, FALSE, TRUE, 0);

		watch->toplevel = vbox;
		watch->cancel_widget = cancel_button;
	}
	else {
		gtk_size_group_add_widget (GTK_SIZE_GROUP (watch->group->user_data1),
		                           title_label);

		/* Construction of grouped watch UI */
		hbox = gtk_hbox_new (FALSE, WATCH_H_SPACING);

		indent = gtk_label_new ("");
		gtk_widget_set_size_request (indent, WATCH_INDENT, 0);

		gtk_box_pack_start (GTK_BOX (hbox), indent, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (hbox), title_label, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (hbox), progress_bar, TRUE, TRUE, 0);

		vbox = watch->group->watch_box;
		gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, WATCH_V_SPACING);

		/* Ensure visible and non-cancelled */
		if (!watch->group->visible)
			show_group (progress_dialog, watch->group);

		gtk_widget_set_sensitive (watch->group->cancel_widget, TRUE);

		watch->toplevel = hbox;
		watch->cancel_widget = NULL;
	}

	gtk_widget_show_all (GTK_WIDGET (watch->toplevel));

	watch->title_label = title_label;
	watch->progress_bar = progress_bar;

	handle_update (progress_monitor, watch);

	if (priv->permanent_mode == TRUE)
		/* Ensure we are visible; quicker just to call than to check */
		gtk_widget_show (GTK_WIDGET (progress_dialog));

	/* Don't focus the cancel button, for the annoying case of the dialog being
	 * popped up while the user is typing. They may still press tab and then
	 * enter to cancel.
	 */
	gtk_window_set_focus (GTK_WINDOW (progress_dialog), NULL);
}


/* remove_watch:
 *
 * Note that this function does not stop the watch pushing status messages. If
 * the watch is not cancelled or complete, caller must handle this.
 */
static void
gtk_iris_progress_dialog_remove_watch (IrisProgressMonitor *progress_monitor,
                                       IrisProgressWatch   *watch)
{
	GtkIrisProgressDialog        *progress_dialog;
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = GTK_IRIS_PROGRESS_DIALOG (progress_monitor);
	priv = progress_dialog->priv;

	if (watch->group != NULL) {
		gtk_size_group_remove_widget (GTK_SIZE_GROUP (watch->group->user_data1),
		                              watch->title_label);

		/* Group list is updated later by the interface, for now this watch
		 * is still in it.
		 */
		g_warn_if_fail (g_list_length (watch->group->watch_list) > 0);

		if (g_list_length (watch->group->watch_list) == 1)
			/* If no watches left in group, hide it group (note this watch gets
			 * removed from the list by the interface so there must be at least 1)
			 */
			hide_group (progress_dialog, watch->group);
		else if (!watch->cancelled)
			/* If watch completed properly, add to count so it is still
			 * counted towards the total group progress
			 */
			watch->group->completed_watches ++;
	}

	gtk_widget_destroy (GTK_WIDGET (watch->toplevel));

	/* Remove self from dialog's watch list */
	priv->watch_list = g_list_remove (priv->watch_list, watch);

	_iris_progress_watch_free (watch);

	update_dialog_title (progress_dialog, NULL);

	/* If no watches left, hide dialog */
	if (priv->watch_list == NULL)
		finish_dialog (progress_dialog);
}

static void
gtk_iris_progress_dialog_reorder_watch_in_group (IrisProgressMonitor *progress_monitor,
                                                 IrisProgressWatch   *watch,
                                                 gboolean             at_end)
{
	GList             *children;
	gint               n_children,
	                   new_position;
	IrisProgressGroup *group;

	g_return_if_fail (watch->group != NULL);

	group = watch->group;

	children = gtk_container_get_children (GTK_CONTAINER (group->watch_box));
	n_children = g_list_length (children);
	g_list_free (children);

	g_return_if_fail (n_children > 0);

	if (at_end)
		new_position = n_children - 1;
	else
		new_position = 0;

	gtk_box_reorder_child (GTK_BOX (group->watch_box),
	                       GTK_WIDGET (watch->toplevel),
	                       new_position);
}

static gboolean
gtk_iris_progress_dialog_is_watching_task (IrisProgressMonitor *progress_monitor,
                                           IrisTask            *task)
{
	return (gtk_iris_progress_dialog_get_watch (progress_monitor, task) != NULL);
}

static void
format_watch_title (GtkWidget   *label,
                    const gchar *title,
                    gboolean     grouped)
{
	gchar *title_formatted;

	if (title == NULL)
		gtk_label_set_text (GTK_LABEL (label), "");
	else {
		if (grouped)
			title_formatted = g_strdup_printf ("%s: ", title);
		else
			title_formatted = g_strdup_printf ("<b>%s</b>", title);

		gtk_label_set_markup (GTK_LABEL (label), title_formatted);
		g_free (title_formatted);
	}
}

/**************************************************************************
 *                        Message processing                              *
 *************************************************************************/

/* No need to worry about locking here, remember these messages are processed in
 * the main loop.
 */

static void
update_dialog_title (GtkIrisProgressDialog *progress_dialog,
                     gchar              *watch_progress_text_precalculated)
{
	GtkIrisProgressDialogPrivate *priv;
	IrisProgressWatch         *head_watch;
	IrisProgressGroup         *head_group;
	GString                   *string;
	char                      *title,
	                          *progress_text;
	char                       progress_text_buffer[256];

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_dialog));

	priv = progress_dialog->priv;

	if (priv->title_is_static) {
		gtk_window_set_title (GTK_WINDOW (progress_dialog),
		                      priv->title_format);
		return;
	}

	if (priv->watch_list == NULL) {
		title = _("Progress Monitor");
		progress_text = NULL;
	} else {
		head_watch = priv->watch_list->data;
		head_group = head_watch->group;

		if (head_group == NULL) {
			title = head_watch->title;

			if (head_watch->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY)
				progress_text = NULL;
			else if (watch_progress_text_precalculated)
				progress_text = watch_progress_text_precalculated;
			else {
				_iris_progress_monitor_format_watch_progress
				    (IRIS_PROGRESS_MONITOR (progress_dialog),
				     head_watch, progress_text_buffer);
				progress_text = progress_text_buffer;
			}
		} else {
			title = head_group->title;

			if (head_group->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY)
				progress_text = NULL;
			else {
				_iris_progress_monitor_format_group_progress
				    (IRIS_PROGRESS_MONITOR (progress_dialog),
				     head_group, progress_text_buffer, NULL);
				progress_text = progress_text_buffer;
			}
		}
	}

	string = g_string_new (NULL);

	if (title != NULL && title[0] != '\0') {
		g_string_append (string, title);
		if (progress_text != NULL)
			g_string_append (string, " - ");
	}

	if (progress_text)
		g_string_append (string, progress_text);

	if (priv->title_format) {
		char *temp = g_string_free (string, FALSE);
		string = g_string_new (NULL);
		g_string_printf (string, priv->title_format, temp);
		g_free (temp);
	}

	gtk_window_set_title (GTK_WINDOW (progress_dialog), string->str);

	g_string_free (string, TRUE);
}

static void
finish_dialog (GtkIrisProgressDialog *progress_dialog)
{
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_dialog));

	priv = progress_dialog->priv;

	/* Emit IrisProgressMonitor::finished. Keep it reffed so that if it gets
	 * destroyed we don't crash. */
	g_object_ref (progress_dialog);

	priv->in_finished = TRUE;
	_iris_progress_monitor_finished (IRIS_PROGRESS_MONITOR (progress_dialog));
	priv->in_finished = FALSE;

	if (priv->permanent_mode) {
		/* Check the 'finished' handler didn't destroy the dialog */
		if (g_atomic_int_get (&G_OBJECT(progress_dialog)->ref_count) <= 1)
			g_warning ("GtkIrisProgressDialog: dialog seems to have been "
			            "destroyed in ::finished signal, but permanent mode "
			            "was enabled. Please turn off permanent mode if you "
			            "don't want it!");

		priv->max_width = 0;

		gtk_widget_hide (GTK_WIDGET (progress_dialog));
	}

	g_object_unref (progress_dialog);
}

/* Called by watch hide timeout */
static gboolean
watch_delayed_remove (gpointer data)
{
	IrisProgressWatch *watch = data;

	gtk_iris_progress_dialog_remove_watch (watch->monitor, watch);

	return FALSE;
}

/* Cancelled or complete */
static void
handle_stopped (IrisProgressMonitor *progress_monitor,
                IrisProgressWatch   *watch)
{
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = GTK_IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	/* Finalize handler disconnects and frees all the watches; if this races
	 * with the watch completing/being cancelled, ignore the message to avoid
	 * confusing the dispose handler. Plus, all the widgets will already be gone
	 * due to the container widget being in dispose.
	 */
	if (priv->in_destruction)
		return;

	/* Set cancel button insensitive */
	if (watch->group == NULL)
		gtk_widget_set_sensitive (GTK_WIDGET (watch->cancel_widget), FALSE);
	else
	if (_iris_progress_group_is_stopped (watch->group))
		gtk_widget_set_sensitive (GTK_WIDGET (watch->group->cancel_widget), FALSE);

	/* Remove watch or add idle to do it after showing it completed for a bit */
	if (priv->watch_hide_delay == 0)
		watch_delayed_remove (watch);
	else
	if (priv->watch_hide_delay == -1);
		/* Never hide watch; for debugging purposes */
	else {
		watch->finish_timeout_id = g_timeout_add (priv->watch_hide_delay,
		                                          watch_delayed_remove,
		                                          watch);
	}
}

static void
handle_update (IrisProgressMonitor *progress_monitor,
               IrisProgressWatch   *watch)
{
	GtkIrisProgressDialogPrivate *priv;
	char                       progress_text[256];
	GtkWidget                 *progress_bar;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = GTK_IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	progress_bar = GTK_WIDGET (watch->progress_bar);

	if (watch->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY)
		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress_bar));
	else {
		_iris_progress_monitor_format_watch_progress (progress_monitor, watch,
		                                              progress_text);

		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar), progress_text);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
		                               watch->fraction);
	}

	if (priv->watch_list->data == watch)
		/* Update title if this is the head watch */
		update_dialog_title (GTK_IRIS_PROGRESS_DIALOG (progress_monitor), progress_text);
}

static void
handle_title (IrisProgressMonitor *progress_monitor,
              IrisProgressWatch   *watch)
{
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	priv = GTK_IRIS_PROGRESS_DIALOG (progress_monitor)->priv;

	format_watch_title (GTK_WIDGET (watch->title_label),
	                    watch->title,
	                    (watch->group != NULL));

	if (priv->watch_list->data == watch)
		update_dialog_title (GTK_IRIS_PROGRESS_DIALOG (progress_monitor), NULL);
}


static void
gtk_iris_progress_dialog_handle_message (IrisProgressMonitor *progress_monitor,
                                         IrisProgressWatch   *watch,
                                         IrisMessage         *message)
{
	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	/* Message has already been parsed by interface and 'watch' updated */

	switch (message->what) {
		case IRIS_PROGRESS_MESSAGE_COMPLETE:
		case IRIS_PROGRESS_MESSAGE_CANCELLED:
			handle_stopped (progress_monitor, watch);
			break;
		case IRIS_PROGRESS_MESSAGE_PULSE:
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
 * gtk_iris_progress_dialog_new:
 * @parent: window to act as the parent of this dialog, or %NULL
 *
 * Creates a new #GtkIrisProgressDialog.
 * 
 * By default, the title of the dialog will be the title and status of the
 * top group or watch, in the following format:
 *   <literal>"Checking Data - 46% complete"</literal>
 * You may set a different window title using gtk_iris_progress_dialog_set_title().
 * Do not use gtk_window_set_title() or your title will be overwritten by
 * status updates.
 *
 * Return value: a newly-created #GtkIrisProgressDialog widget
 */
GtkWidget *
gtk_iris_progress_dialog_new (GtkWindow   *parent)
{
	GtkWidget *progress_dialog = g_object_new (GTK_IRIS_TYPE_PROGRESS_DIALOG, NULL);

	gtk_window_set_transient_for (GTK_WINDOW (progress_dialog), parent);

	return progress_dialog;
}


/**
 * gtk_iris_progress_dialog_set_title:
 * @progress_dialog: an #GtkIrisProgressDialog
 * @title_format: string to set as the window title for @progress_dialog
 *
 * This function sets the window title of @progress_dialog. By default the
 * dialog title is the status of the first process or group that is being
 * watched, and you may include this in @title_format using the marker
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
 * <note>
 * <para>
 * You cannot use gtk_window_set_title() to set the dialog's title, because the
 * title will be overwritten when the progress changes.
 * </para>
 * </note>
 **/
void
gtk_iris_progress_dialog_set_title (GtkIrisProgressDialog *progress_dialog,
                                    const gchar        *title_format)
{
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_dialog));

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

	update_dialog_title (progress_dialog, NULL);
};

void
gtk_iris_progress_dialog_set_permanent_mode (IrisProgressMonitor *progress_monitor,
                                             gboolean             enable)
{
	GtkIrisProgressDialog        *progress_dialog;
	GtkIrisProgressDialogPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = GTK_IRIS_PROGRESS_DIALOG (progress_monitor);
	priv = progress_dialog->priv;

	priv->permanent_mode = enable;

	if (enable) {
		if (priv->watch_list == NULL)
			gtk_widget_hide (GTK_WIDGET (progress_dialog));
		else
			gtk_widget_show (GTK_WIDGET (progress_dialog));
	}
}

void
gtk_iris_progress_dialog_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                               gint                 milliseconds)
{
	GtkIrisProgressDialog *progress_dialog;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor));

	progress_dialog = GTK_IRIS_PROGRESS_DIALOG (progress_monitor);

	progress_dialog->priv->watch_hide_delay = milliseconds;
}

/* Private interface member */
IrisProgressWatch *
gtk_iris_progress_dialog_get_watch (IrisProgressMonitor *progress_monitor,
                                    IrisTask            *task)
{
	GtkIrisProgressDialog        *progress_dialog;
	GtkIrisProgressDialogPrivate *priv;
	GList                         *node;

	g_return_val_if_fail (GTK_IRIS_IS_PROGRESS_DIALOG (progress_monitor), NULL);
	g_return_val_if_fail (IRIS_IS_TASK (task), NULL);

	progress_dialog = GTK_IRIS_PROGRESS_DIALOG (progress_monitor);

	priv = progress_dialog->priv;

	node = g_list_find_custom (priv->watch_list, task, find_watch_by_task);

	return node? node->data: NULL;
}

