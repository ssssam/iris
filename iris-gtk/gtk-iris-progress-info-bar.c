/* iris-progress-info-bar.c
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
#include "gtk-iris-progress-info-bar.h"
#include "gtk-iris-progress-info-bar-private.h"

/**
 * SECTION:gtk-iris-progress-info-bar
 * @title: GtkIrisProgressInfoBar
 * @short_description: GtkInfoBar showing progress of tasks and processes.
 * @see_also: #GtkIrisProgressDialog
 * @include: iris/iris-gtk.h
 *
 * #GtkIrisProgressInfoBar creates a #GtkInfobar which shows the status of various
 * #IrisProcess and #IrisTask objects in a small bar you can place at the top
 * or bottom of an application window. It tries to be as compact as possible,
 * showing a single-line summary of each group by default and a #GtkExpander
 * control to show the individual progress bars if the user wishes.
 * individual progress bars.
 *
 * Use the #IrisProgressMonitor interface to control the info bar. The tasks
 * being monitored will communicate by passing messages to the
 * #GtkIrisProgressDialog, which enables the Gtk+ work to be done entirely from
 * the GLib main loop thread. You may not call any of the #IrisProgressMonitor
 * interface methods outside of the GLib main loop thread, as with all portable
 * Gtk+ code.
 */

/* Note that iris-progress-dialog.c is better commented where the two share code */

static void     gtk_iris_progress_info_bar_class_init          (GtkIrisProgressInfoBarClass *progress_info_bar_class);
static void     gtk_iris_progress_info_bar_init                (GtkIrisProgressInfoBar *progress_info_bar);
static void     iris_progress_monitor_interface_init           (IrisProgressMonitorInterface *interface);

static GObject *gtk_iris_progress_info_bar_constructor         (GType type,
                                                                guint n_construct_properties,
                                                                GObjectConstructParam *construct_params);
static void     gtk_iris_progress_info_bar_dispose             (GObject *object);
static void     gtk_iris_progress_info_bar_finalize            (GObject *object);

static void     gtk_iris_progress_info_bar_add_group              (IrisProgressMonitor *progress_monitor,
                                                                   IrisProgressGroup   *group);
static void     gtk_iris_progress_info_bar_remove_group           (IrisProgressMonitor *progress_monitor,
                                                                   IrisProgressGroup   *group);
static void     gtk_iris_progress_info_bar_add_watch              (IrisProgressMonitor *progress_monitor,
                                                                   IrisProgressWatch   *watch);
static void     gtk_iris_progress_info_bar_remove_watch           (IrisProgressMonitor *progress_monitor,
                                                                   IrisProgressWatch   *watch);
static void     gtk_iris_progress_info_bar_reorder_watch_in_group (IrisProgressMonitor *progress_monitor,
                                                                   IrisProgressWatch   *watch,
                                                                   gboolean             at_end);

static void     handle_update                                   (IrisProgressMonitor *progress_monitor,
                                                                 IrisProgressWatch   *watch);
static void     gtk_iris_progress_info_bar_handle_message       (IrisProgressMonitor *progress_monitor,
                                                                 IrisProgressWatch   *watch,
                                                                 IrisMessage         *message);

static gboolean gtk_iris_progress_info_bar_is_watching_task     (IrisProgressMonitor *progress_monitor,
                                                                 IrisTask            *task);

static void     format_watch_title                              (GtkWidget           *label,
                                                                 const gchar         *title,
                                                                 gboolean             grouped);

static void     finish_info_bar                                 (GtkIrisProgressInfoBar *progress_info_bar);

static void     gtk_iris_progress_info_bar_set_permanent_mode   (IrisProgressMonitor *progress_monitor,
                                                                 gboolean             enable);
static void     gtk_iris_progress_info_bar_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                                                 int                  milliseconds);

IrisProgressWatch *gtk_iris_progress_info_bar_get_watch         (IrisProgressMonitor *progress_monitor,
                                                                 IrisTask            *task);

G_DEFINE_TYPE_WITH_CODE (GtkIrisProgressInfoBar, gtk_iris_progress_info_bar, GTK_TYPE_INFO_BAR,
                         G_IMPLEMENT_INTERFACE (IRIS_TYPE_PROGRESS_MONITOR,
                                                iris_progress_monitor_interface_init))

#define H_SPACING 4
#define GROUP_V_SPACING 2

#define WATCH_V_SPACING 4
#define WATCH_INDENT 18

/* Based on Gtk+ defaults */
#define EXPANDER_INDENT 18

static void
gtk_iris_progress_info_bar_class_init (GtkIrisProgressInfoBarClass *progress_info_bar_class)
{
	GtkInfoBarClass *info_bar_class = GTK_INFO_BAR_CLASS(progress_info_bar_class);
	GObjectClass   *object_class = G_OBJECT_CLASS  (info_bar_class);

	object_class->constructor = gtk_iris_progress_info_bar_constructor;
	object_class->dispose     = gtk_iris_progress_info_bar_dispose;
	object_class->finalize    = gtk_iris_progress_info_bar_finalize;

	gtk_iris_progress_info_bar_parent_class = g_type_class_peek_parent (info_bar_class);

	g_type_class_add_private (object_class, sizeof(GtkIrisProgressInfoBarPrivate));
}

static void
gtk_iris_progress_info_bar_init (GtkIrisProgressInfoBar *progress_info_bar)
{
	GtkIrisProgressInfoBarPrivate *priv;

	priv = GTK_IRIS_PROGRESS_INFO_BAR_GET_PRIVATE (progress_info_bar);

	progress_info_bar->priv = priv;

	priv->scheduler = iris_gmainscheduler_new (NULL);

	priv->watch_list = NULL;

	priv->watch_hide_delay = 750;

	priv->in_finished = FALSE;
	priv->permanent_mode = FALSE;
}

static void
gtk_iris_progress_info_bar_dispose (GObject *object)
{
	GtkIrisProgressInfoBarPrivate *priv;

	priv = GTK_IRIS_PROGRESS_INFO_BAR (object)->priv;

	priv->in_destruction = TRUE;
	G_OBJECT_CLASS (gtk_iris_progress_info_bar_parent_class)->dispose (object);
}

static void
gtk_iris_progress_info_bar_finalize (GObject *object)
{
	GList *node;
	GtkIrisProgressInfoBar        *info_bar = GTK_IRIS_PROGRESS_INFO_BAR (object);
	GtkIrisProgressInfoBarPrivate *priv     = info_bar->priv;

	for (node=priv->watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		if (watch->finish_timeout_id != 0)
			g_source_remove (watch->finish_timeout_id);

		_iris_progress_watch_disconnect (node->data);
		_iris_progress_watch_free (node->data);
	}

	G_OBJECT_CLASS(gtk_iris_progress_info_bar_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_group            = gtk_iris_progress_info_bar_add_group;
	interface->add_watch            = gtk_iris_progress_info_bar_add_watch;
	interface->handle_message       = gtk_iris_progress_info_bar_handle_message;
	interface->is_watching_task     = gtk_iris_progress_info_bar_is_watching_task;
	interface->set_permanent_mode   = gtk_iris_progress_info_bar_set_permanent_mode;
	interface->set_watch_hide_delay = gtk_iris_progress_info_bar_set_watch_hide_delay;

	/* Private */
	interface->remove_group           = gtk_iris_progress_info_bar_remove_group;
	interface->remove_watch           = gtk_iris_progress_info_bar_remove_watch;
	interface->reorder_watch_in_group = gtk_iris_progress_info_bar_reorder_watch_in_group;
	interface->get_watch              = gtk_iris_progress_info_bar_get_watch;
}

static GObject *
gtk_iris_progress_info_bar_constructor (GType type,
                                        guint n_construct_properties,
                                        GObjectConstructParam *construct_params)
{
	GtkWidget  *content_area,
	           *action_area;

	GObject    *object = ((GObjectClass *) gtk_iris_progress_info_bar_parent_class)->constructor
	                      (type, n_construct_properties, construct_params);
	GtkInfoBar *info_bar = GTK_INFO_BAR (object);

	GtkIrisProgressInfoBar *progress_info_bar = GTK_IRIS_PROGRESS_INFO_BAR (info_bar);
	GtkIrisProgressInfoBarPrivate *priv = progress_info_bar->priv;

	content_area = gtk_info_bar_get_content_area (info_bar);
	priv->box = gtk_vbox_new (FALSE, GROUP_V_SPACING);
	gtk_container_add (GTK_CONTAINER (content_area), priv->box);
	gtk_widget_show (priv->box);

	/* Action area hack - we add our own vbox to hold the cancel buttons. We
	 * cannot use the real action area because it's a GtkButtonBox so the
	 * spacing has to be uniform. This doesn't work because the buttons need to
	 * line up with the group progress widgets, which can be any size.
	 */
	action_area = gtk_info_bar_get_action_area (info_bar);
	priv->action_box = gtk_vbox_new (FALSE, GROUP_V_SPACING);
	gtk_box_pack_end (GTK_BOX (action_area), priv->action_box, TRUE, TRUE, 0);
	gtk_widget_show (priv->action_box);

	/* FIXME: GTK_MESSAGE_PROGRESS ?? */
	gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar),
	                               GTK_MESSAGE_INFO);

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
group_cancel_clicked (GtkButton *cancel_button,
                      gpointer   user_data) {
	IrisProgressMonitor *progress_monitor;
	IrisProgressGroup   *group;

	gtk_widget_set_sensitive (GTK_WIDGET (cancel_button), FALSE);

	progress_monitor = IRIS_PROGRESS_MONITOR (user_data);
	group = g_object_get_data (G_OBJECT (cancel_button), "group");

	_iris_progress_monitor_cancel_group (progress_monitor, group);
}

static void
gtk_iris_progress_info_bar_add_group (IrisProgressMonitor *progress_monitor,
                                      IrisProgressGroup   *group) {
	GtkIrisProgressInfoBar        *info_bar;
	GtkIrisProgressInfoBarPrivate *priv;
	GtkWidget    *expander,
	             *hbox, *vbox,
	             *title_label,
	             *progress_bar,
	             *cancel_button, *cancel_alignment;
	gchar        *title;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = GTK_IRIS_PROGRESS_INFO_BAR (progress_monitor);
	priv = info_bar->priv;

	/* To align titles of member watches */
	group->user_data1 = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	expander = gtk_expander_new (NULL);

	hbox = gtk_hbox_new (FALSE, H_SPACING);

	title = g_strdup_printf ("<b>%s</b>", group->title);
	title_label = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL(title_label), title);
	gtk_misc_set_alignment (GTK_MISC (title_label), 1.0, 0.5);
	g_free (title);

	progress_bar = gtk_progress_bar_new ();

	gtk_box_pack_start (GTK_BOX (hbox), title_label, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), progress_bar, TRUE, FALSE, 0);

	vbox = gtk_vbox_new (FALSE, WATCH_V_SPACING);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), WATCH_V_SPACING);

	gtk_expander_set_label_widget (GTK_EXPANDER (expander), hbox);
	gtk_container_add (GTK_CONTAINER (expander), vbox);

	cancel_button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
	cancel_alignment = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);
	gtk_container_add (GTK_CONTAINER (cancel_alignment), cancel_button);

	g_object_set_data (G_OBJECT (cancel_button), "group", group);
	g_signal_connect (G_OBJECT (cancel_button),
	                  "clicked",
	                  G_CALLBACK (group_cancel_clicked),
	                  progress_monitor);

	/* Give cancel button a matching size to its progress widgets */
	group->user_data2 = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
	gtk_size_group_add_widget (GTK_SIZE_GROUP (group->user_data2), expander);
	gtk_size_group_add_widget (GTK_SIZE_GROUP (group->user_data2), cancel_alignment);

	/* Make the progress bar the right height - ungrouped watches have them the
	 * same height as the cancel button; ours need to be the same
	 */
	group->user_data3 = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
	gtk_size_group_add_widget (GTK_SIZE_GROUP (group->user_data3), progress_bar);
	gtk_size_group_add_widget (GTK_SIZE_GROUP (group->user_data3), cancel_button);

	g_object_ref_sink (G_OBJECT (expander));
	g_object_ref_sink (G_OBJECT (cancel_alignment));

	group->toplevel = expander;
	group->watch_box = vbox;
	group->progress_bar = progress_bar;
	group->cancel_widget = cancel_alignment;
}

static void
gtk_iris_progress_info_bar_remove_group (IrisProgressMonitor *progress_monitor,
                                         IrisProgressGroup   *group)
{
	g_return_if_fail (GTK_IS_WIDGET (group->toplevel));
	g_warn_if_fail (G_OBJECT (group->toplevel)->ref_count == 1);

	g_return_if_fail (GTK_IS_WIDGET (group->cancel_widget));
	g_warn_if_fail (G_OBJECT (group->cancel_widget)->ref_count == 1);

	gtk_widget_destroy (group->toplevel);
	gtk_widget_destroy (group->cancel_widget);

	g_object_unref (group->user_data1);
	g_object_unref (group->user_data2);
	g_object_unref (group->user_data3);
}

static void
show_group (GtkIrisProgressInfoBar *progress_info_bar,
            IrisProgressGroup      *group)
{
	GtkIrisProgressInfoBarPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar));
	g_return_if_fail (group->visible == FALSE);

	priv = progress_info_bar->priv;

	gtk_box_pack_start (GTK_BOX (priv->box), group->toplevel, FALSE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (group->toplevel));

	gtk_box_pack_start (GTK_BOX (priv->action_box), group->cancel_widget, FALSE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (group->cancel_widget));

	group->visible = TRUE;
}

static void
hide_group (GtkIrisProgressInfoBar *progress_info_bar,
            IrisProgressGroup      *group)
{
	GtkIrisProgressInfoBarPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar));
	g_return_if_fail (group->visible == TRUE);

	priv = progress_info_bar->priv;

	gtk_container_remove (GTK_CONTAINER (priv->box), group->toplevel);
	gtk_container_remove (GTK_CONTAINER (priv->action_box), group->cancel_widget);

	_iris_progress_group_reset (group);

	group->visible = FALSE;
}



static void
watch_cancel_clicked (GtkButton *cancel_button,
                      gpointer   user_data) {
	IrisProgressMonitor *progress_monitor;
	IrisProgressWatch   *watch;

	gtk_widget_set_sensitive (GTK_WIDGET (cancel_button), FALSE);

	progress_monitor = IRIS_PROGRESS_MONITOR (user_data);
	watch = g_object_get_data (G_OBJECT (cancel_button), "watch");

	_iris_progress_monitor_cancel_watch (progress_monitor, watch);
}

static void
gtk_iris_progress_info_bar_add_watch (IrisProgressMonitor *progress_monitor,
                                      IrisProgressWatch   *watch)
{
	GtkIrisProgressInfoBar        *progress_info_bar;
	GtkIrisProgressInfoBarPrivate *priv;
	IrisProgressGroup             *group;

	GtkWidget    *vbox, *hbox, *hbox_indent,
	             *title_label,
	             *progress_bar,
	             *cancel_button, *cancel_alignment;
	GtkSizeGroup *size_group;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = GTK_IRIS_PROGRESS_INFO_BAR (progress_monitor);
	priv = progress_info_bar->priv;

	g_return_if_fail (!priv->in_finished);

	/* Add watch */

	watch->receiver = iris_arbiter_receive
	                    (priv->scheduler, watch->port, 
	                     _iris_progress_monitor_handle_message,
	                     watch, NULL);

	priv->watch_list = g_list_append (priv->watch_list, watch);

	group = watch->group;

	if (group != NULL)
		/* If we have 3 watches updating the group progress bar, in activity
		 * mode it needs to move at 1/3 the speed ...
		 */
		gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (group->progress_bar),
		                                 0.1 / g_list_length (group->watch_list));

	/* Add UI for watch */

	hbox_indent = gtk_hbox_new (FALSE, 0);
	hbox = gtk_hbox_new (FALSE, H_SPACING);

	title_label = gtk_label_new (watch->title);
	gtk_misc_set_alignment (GTK_MISC (title_label), 1.0, 0.5);
	format_watch_title (title_label, watch->title, watch->group != NULL);

	progress_bar = gtk_progress_bar_new ();

	gtk_box_pack_start (GTK_BOX (hbox), title_label, FALSE, TRUE, 0);

	if (group == NULL) {
		gtk_box_pack_start (GTK_BOX (hbox), progress_bar, FALSE, TRUE, 0);

		gtk_box_pack_start (GTK_BOX (hbox_indent), hbox, FALSE, TRUE, EXPANDER_INDENT);
		gtk_box_pack_start (GTK_BOX (priv->box), hbox_indent, FALSE, TRUE, 0);

		cancel_button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
		cancel_alignment = gtk_alignment_new (0.5, 0.0, 0.0, 0.0);
		gtk_container_add (GTK_CONTAINER (cancel_alignment), cancel_button);

		g_object_set_data (G_OBJECT (cancel_button), "watch", watch);
		g_signal_connect (G_OBJECT (cancel_button),
		                  "clicked",
		                  G_CALLBACK (watch_cancel_clicked),
		                  progress_monitor);

		/* Maintain relationship between our cancel button in the action widgets
		 * hbox and the actual progress monitor widgets
		 */
		size_group = gtk_size_group_new (GTK_SIZE_GROUP_VERTICAL);
		gtk_size_group_add_widget (GTK_SIZE_GROUP (size_group), hbox_indent);
		gtk_size_group_add_widget (GTK_SIZE_GROUP (size_group),
		                           cancel_alignment);

		gtk_box_pack_start (GTK_BOX (priv->action_box), cancel_alignment, TRUE, TRUE, 0);
		gtk_widget_show_all (cancel_alignment);

		watch->cancel_widget = cancel_alignment;
		watch->user_data2 = size_group;
	}
	else {
		gtk_box_pack_start (GTK_BOX (hbox), progress_bar, TRUE, TRUE, 0);

		gtk_size_group_add_widget (GTK_SIZE_GROUP (group->user_data1),
		                           title_label);

		hbox_indent = gtk_hbox_new (FALSE, 0);
		gtk_box_pack_start (GTK_BOX (hbox_indent), hbox, TRUE, TRUE,
		                    WATCH_INDENT + EXPANDER_INDENT);

		vbox = watch->group->watch_box;
		gtk_box_pack_start (GTK_BOX (vbox), hbox_indent, FALSE, TRUE, 0);

		if (!watch->group->visible)
			show_group (progress_info_bar, group);

		cancel_button = gtk_bin_get_child (GTK_BIN (watch->group->cancel_widget));
		gtk_widget_set_sensitive (cancel_button, TRUE);

		watch->cancel_widget = NULL;
		watch->user_data2 = NULL;
	}

	gtk_widget_show_all (hbox_indent);

	watch->toplevel = hbox_indent;
	watch->title_label = title_label;
	watch->progress_bar = progress_bar;

	handle_update (progress_monitor, watch);

	if (priv->permanent_mode == TRUE)
		/* Ensure we are visible; quicker just to call than to check */
		gtk_widget_show (GTK_WIDGET (progress_info_bar));
}

static void
gtk_iris_progress_info_bar_remove_watch (IrisProgressMonitor *progress_monitor,
                                         IrisProgressWatch   *watch)
{
	GtkIrisProgressInfoBar        *progress_info_bar;
	GtkIrisProgressInfoBarPrivate *priv;
	IrisProgressGroup             *group;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (watch->monitor));

	progress_info_bar = GTK_IRIS_PROGRESS_INFO_BAR (watch->monitor);
	priv = progress_info_bar->priv;

	group = watch->group;

	if (group != NULL) {
		gtk_size_group_remove_widget (GTK_SIZE_GROUP (group->user_data1),
		                              watch->title_label);

		g_warn_if_fail (g_list_length (group->watch_list) > 0);

		if (g_list_length (group->watch_list) == 1) {
			g_warn_if_fail (group->watch_list->data == watch);
			hide_group (progress_info_bar, group);
		} else
		if (!watch->cancelled) {
			group->completed_watches ++;

			gtk_progress_bar_set_pulse_step
			  (GTK_PROGRESS_BAR (group->progress_bar),
			   0.1 / g_list_length (group->watch_list));
		}
	} else {
		/* Remove cancel button from action area */
		gtk_container_remove (GTK_CONTAINER (priv->action_box),
		                      watch->cancel_widget);

		g_object_unref (GTK_SIZE_GROUP (watch->user_data2));
	}

	gtk_widget_destroy (GTK_WIDGET (watch->toplevel));

	priv->watch_list = g_list_remove (priv->watch_list, watch);

	_iris_progress_watch_free (watch);

	/* If no watches left, hide dialog */
	if (priv->watch_list == NULL)
		finish_info_bar (progress_info_bar);
}

static void
gtk_iris_progress_info_bar_reorder_watch_in_group (IrisProgressMonitor *progress_monitor,
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
gtk_iris_progress_info_bar_is_watching_task (IrisProgressMonitor *progress_monitor,
                                             IrisTask            *task)
{
	return (gtk_iris_progress_info_bar_get_watch (progress_monitor, task) != NULL);
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

static void
finish_info_bar (GtkIrisProgressInfoBar *progress_info_bar)
{
	GtkIrisProgressInfoBarPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar));

	priv = progress_info_bar->priv;

	/* Emit IrisProgressMonitor::finished */
	g_object_ref (progress_info_bar);

	priv->in_finished = TRUE;
	_iris_progress_monitor_finished (IRIS_PROGRESS_MONITOR (progress_info_bar));
	priv->in_finished = FALSE;

	if (priv->permanent_mode) {
		/* Check the 'finished' handler didn't destroy the info bar */
		if (g_atomic_int_get (&G_OBJECT(progress_info_bar)->ref_count) <= 1)
			g_warning ("GtkIrisProgressInfoBar: widget seems to have been "
			            "destroyed in ::finished signal, but permanent mode "
			            "was enabled. Please turn off permanent mode if you "
			            "don't want it!");

		gtk_widget_hide (GTK_WIDGET (progress_info_bar));
	}

	g_object_unref (progress_info_bar);
}

static gboolean
watch_delayed_remove (gpointer data)
{
	IrisProgressWatch *watch = data;

	gtk_iris_progress_info_bar_remove_watch (watch->monitor, watch);

	return FALSE;
}

/* When a watch completes/is cancelled, we check to see if any are still running
 * and if they are all done, we self destruct. */
static void
handle_stopped (IrisProgressMonitor *progress_monitor,
                IrisProgressWatch   *watch)
{
	GtkIrisProgressInfoBarPrivate *priv;
	GtkWidget                     *cancel_button;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	priv = GTK_IRIS_PROGRESS_INFO_BAR (progress_monitor)->priv;

	if (priv->in_destruction)
		return;

	if (watch->group == NULL) {
		cancel_button = gtk_bin_get_child (GTK_BIN (watch->cancel_widget));
		gtk_widget_set_sensitive (cancel_button, FALSE);
	} else
	if (_iris_progress_group_is_stopped (watch->group)) {
		cancel_button = gtk_bin_get_child (GTK_BIN (watch->group->cancel_widget));
		gtk_widget_set_sensitive (cancel_button, FALSE);
	}

	if (priv->watch_hide_delay == 0)
		watch_delayed_remove (watch);
	else
	if (priv->watch_hide_delay == -1);
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
	char                 progress_text[256];
	GtkIrisProgressInfoBar *info_bar;
	GtkWidget           *progress_bar;
	gdouble              group_fraction;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = GTK_IRIS_PROGRESS_INFO_BAR (progress_monitor);

	progress_bar = GTK_WIDGET (watch->progress_bar);

	if (watch->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY)
		gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress_bar));
	else {
		_iris_progress_monitor_format_watch_progress (progress_monitor, watch,
		                                              progress_text);
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar),
		                           progress_text);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
		                               watch->fraction);
	}

	if (watch->group != NULL) {
		progress_bar = GTK_WIDGET (watch->group->progress_bar);

		if (watch->group->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY) {
			gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress_bar));
		} else {
			_iris_progress_monitor_format_group_progress
			  (progress_monitor, watch->group, progress_text, &group_fraction);
			gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar),
			                           progress_text);
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
			                               group_fraction);
		}
	}
};

static void
handle_title (IrisProgressMonitor *progress_monitor,
              IrisProgressWatch   *watch)
{
	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	format_watch_title (watch->title_label, watch->title, watch->group != NULL);
}


static void
gtk_iris_progress_info_bar_handle_message (IrisProgressMonitor *progress_monitor,
                                           IrisProgressWatch   *watch,
                                           IrisMessage         *message)
{
	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR(progress_monitor));

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
 * gtk_iris_progress_info_bar_new:
 *
 * Creates a new #GtkIrisProgressInfoBar.
 *
 * Return value: a newly-created #GtkIrisProgressInfoBar widget
 */
GtkWidget *
gtk_iris_progress_info_bar_new ()
{
	GtkWidget *progress_info_bar = g_object_new (GTK_IRIS_TYPE_PROGRESS_INFO_BAR, NULL);

	return progress_info_bar;
}

static void
gtk_iris_progress_info_bar_set_permanent_mode (IrisProgressMonitor *progress_monitor,
                                               gboolean             enable)
{
	GtkIrisProgressInfoBar        *progress_info_bar;
	GtkIrisProgressInfoBarPrivate *priv;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = GTK_IRIS_PROGRESS_INFO_BAR (progress_monitor);
	priv = progress_info_bar->priv;

	priv->permanent_mode = enable;

	if (enable) {
		if (priv->watch_list == NULL)
			gtk_widget_hide (GTK_WIDGET (progress_info_bar));
		else
			gtk_widget_show (GTK_WIDGET (progress_info_bar));
	}
}

static void
gtk_iris_progress_info_bar_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                                 gint                 milliseconds)
{
	GtkIrisProgressInfoBar *progress_info_bar;

	g_return_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = GTK_IRIS_PROGRESS_INFO_BAR (progress_monitor);

	progress_info_bar->priv->watch_hide_delay = milliseconds;
}

/* Private interface member */
IrisProgressWatch *
gtk_iris_progress_info_bar_get_watch (IrisProgressMonitor *progress_monitor,
                                      IrisTask            *task)
{
	GtkIrisProgressInfoBar        *progress_info_bar;
	GtkIrisProgressInfoBarPrivate *priv;
	GList                         *node;

	g_return_val_if_fail (GTK_IRIS_IS_PROGRESS_INFO_BAR (progress_monitor), NULL);
	g_return_val_if_fail (IRIS_IS_TASK (task), NULL);

	progress_info_bar = GTK_IRIS_PROGRESS_INFO_BAR (progress_monitor);

	priv = progress_info_bar->priv;

	node = g_list_find_custom (priv->watch_list, task, find_watch_by_task);

	return node? node->data: NULL;
}

