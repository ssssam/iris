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
#include "iris-progress-info-bar.h"
#include "iris-progress-info-bar-private.h"

/**
 * SECTION:iris-progress-info-bar
 * @title: IrisProgressInfoBar
 * @short_description: GtkInfoBar showing progress of tasks and processes.
 * @see_also: #IrisProgressDialog
 * @include: iris/iris-gtk.h
 *
 * #IrisProgressInfoBar creates a #GtkInfobar which shows the status of various
 * #IrisProcess and #IrisTask objects in a small bar you can place at the top
 * or bottom of an application window. It tries to be as compact as possible,
 * showing a single-line summary of each group by default and a #GtkExpander
 * control to show the individual progress bars if the user wishes.
 * individual progress bars.
 *
 * Use the #IrisProgressMonitor interface to control the info bar.
 */

/* Note that iris-progress-dialog.c is better commented where the two share code */

static void     iris_progress_info_bar_class_init          (IrisProgressInfoBarClass *progress_info_bar_class);
static void     iris_progress_info_bar_init                (IrisProgressInfoBar *progress_info_bar);
static void     iris_progress_monitor_interface_init       (IrisProgressMonitorInterface *interface);

static GObject *iris_progress_info_bar_constructor         (GType type,
                                                            guint n_construct_properties,
                                                            GObjectConstructParam *construct_params);
static void     iris_progress_info_bar_finalize            (GObject *object);

static void     iris_progress_info_bar_add_group           (IrisProgressMonitor *progress_monitor,
                                                            IrisProgressGroup   *group);
static void     iris_progress_info_bar_remove_group        (IrisProgressMonitor *progress_monitor,
                                                            IrisProgressGroup   *group);
static void     iris_progress_info_bar_add_watch           (IrisProgressMonitor *progress_monitor,
                                                            IrisProgressWatch   *watch);
static void     iris_progress_info_bar_remove_watch        (IrisProgressMonitor *progress_monitor,
                                                            IrisProgressWatch   *watch,
                                                            gboolean             temporary);

static void     iris_progress_info_bar_handle_message       (IrisProgressMonitor *progress_monitor,
                                                             IrisProgressWatch   *watch,
                                                             IrisMessage         *message);

static gboolean iris_progress_info_bar_is_watching_task     (IrisProgressMonitor *progress_monitor,
                                                             IrisTask            *task);

static void     format_watch_title                          (GtkWidget           *label,
                                                             const gchar         *title,
                                                             gboolean             grouped);

static void     finish_info_bar                             (IrisProgressInfoBar *progress_info_bar);

static void     iris_progress_info_bar_set_permanent_mode   (IrisProgressMonitor *progress_monitor,
                                                             gboolean             enable);
static void     iris_progress_info_bar_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                                             int                  milliseconds);

IrisProgressWatch *iris_progress_info_bar_get_watch         (IrisProgressMonitor *progress_monitor,
                                                             IrisTask            *task);

G_DEFINE_TYPE_WITH_CODE (IrisProgressInfoBar, iris_progress_info_bar, GTK_TYPE_INFO_BAR,
                         G_IMPLEMENT_INTERFACE (IRIS_TYPE_PROGRESS_MONITOR,
                                                iris_progress_monitor_interface_init))

#define H_SPACING 4
#define GROUP_V_SPACING 2

#define WATCH_V_SPACING 4
#define WATCH_INDENT 18

/* Based on Gtk+ defaults */
#define EXPANDER_INDENT 18

static void
iris_progress_info_bar_class_init (IrisProgressInfoBarClass *progress_info_bar_class)
{
	GtkInfoBarClass *info_bar_class = GTK_INFO_BAR_CLASS(progress_info_bar_class);
	GObjectClass   *object_class = G_OBJECT_CLASS  (info_bar_class);

	object_class->constructor = iris_progress_info_bar_constructor;
	object_class->finalize    = iris_progress_info_bar_finalize;

	iris_progress_info_bar_parent_class = g_type_class_peek_parent (info_bar_class);

	g_type_class_add_private (object_class, sizeof(IrisProgressInfoBarPrivate));
}

static void
iris_progress_info_bar_init (IrisProgressInfoBar *progress_info_bar)
{
	IrisProgressInfoBarPrivate *priv;

	priv = IRIS_PROGRESS_INFO_BAR_GET_PRIVATE (progress_info_bar);

	progress_info_bar->priv = priv;

	priv->scheduler = iris_gmainscheduler_new (NULL);

	priv->watch_list = NULL;

	priv->watch_hide_delay = 750;

	priv->in_finished = FALSE;
	priv->permanent_mode = FALSE;
}

static void
iris_progress_info_bar_finalize (GObject *object)
{
	GList *node;
	IrisProgressInfoBar        *info_bar = IRIS_PROGRESS_INFO_BAR (object);
	IrisProgressInfoBarPrivate *priv     = info_bar->priv;

	for (node=priv->watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		if (watch->finish_timeout_id != 0)
			g_source_remove (watch->finish_timeout_id);

		_iris_progress_watch_disconnect (node->data);
		_iris_progress_watch_free (node->data);
	}

	G_OBJECT_CLASS(iris_progress_info_bar_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_group            = iris_progress_info_bar_add_group;
	interface->add_watch            = iris_progress_info_bar_add_watch;
	interface->handle_message       = iris_progress_info_bar_handle_message;
	interface->is_watching_task     = iris_progress_info_bar_is_watching_task;
	interface->set_permanent_mode   = iris_progress_info_bar_set_permanent_mode;
	interface->set_watch_hide_delay = iris_progress_info_bar_set_watch_hide_delay;

	/* Private */
	interface->remove_watch         = iris_progress_info_bar_remove_watch;
	interface->get_watch            = iris_progress_info_bar_get_watch;
	interface->remove_group         = iris_progress_info_bar_remove_group;
}

static GObject *
iris_progress_info_bar_constructor (GType type,
                                    guint n_construct_properties,
                                    GObjectConstructParam *construct_params)
{
	GtkWidget  *content_area;

	GObject    *object = ((GObjectClass *) iris_progress_info_bar_parent_class)->constructor
	                      (type, n_construct_properties, construct_params);
	GtkInfoBar *info_bar = GTK_INFO_BAR (object);

	IrisProgressInfoBar *progress_info_bar = IRIS_PROGRESS_INFO_BAR (info_bar);
	IrisProgressInfoBarPrivate *priv = progress_info_bar->priv;

	priv->box = gtk_vbox_new (FALSE, GROUP_V_SPACING);
	gtk_widget_show (priv->box);

	content_area = gtk_info_bar_get_content_area (info_bar);
	gtk_container_add (GTK_CONTAINER (content_area), priv->box);

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
iris_progress_info_bar_add_group (IrisProgressMonitor *progress_monitor,
                                  IrisProgressGroup   *group) {
	IrisProgressInfoBar        *info_bar;
	IrisProgressInfoBarPrivate *priv;
	GtkWidget *expander,
	          *hbox, *vbox,
	          *title_label, *progress_bar;
	gchar     *title;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);
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

	g_object_ref_sink (G_OBJECT (expander));

	group->toplevel = expander;
	group->watch_box = vbox;
	group->progress_bar = progress_bar;
}

static void
iris_progress_info_bar_remove_group (IrisProgressMonitor *progress_monitor,
                                     IrisProgressGroup   *group)
{
	g_return_if_fail (GTK_IS_WIDGET (group->toplevel));
	g_warn_if_fail (G_OBJECT (group->toplevel)->ref_count == 1);

	g_object_unref (group->user_data1);

	gtk_widget_destroy (group->toplevel);
}

static void
show_group (IrisProgressInfoBar *progress_info_bar,
            IrisProgressGroup   *group)
{
	IrisProgressInfoBarPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar));
	g_return_if_fail (group->visible == FALSE);

	priv = progress_info_bar->priv;

	gtk_box_pack_start (GTK_BOX (priv->box), group->toplevel, FALSE, TRUE, 0);
	gtk_widget_show_all (GTK_WIDGET (group->toplevel));

	group->visible = TRUE;
}

static void
hide_group (IrisProgressInfoBar *progress_info_bar,
            IrisProgressGroup   *group)
{
	IrisProgressInfoBarPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar));
	g_return_if_fail (group->visible == TRUE);

	priv = progress_info_bar->priv;

	gtk_container_remove (GTK_CONTAINER (priv->box), group->toplevel);

	_iris_progress_group_reset (group);

	group->visible = FALSE;
}


static void
iris_progress_info_bar_add_watch (IrisProgressMonitor *progress_monitor,
                                  IrisProgressWatch   *watch)
{
	IrisProgressInfoBar        *progress_info_bar;
	IrisProgressInfoBarPrivate *priv;

	GtkWidget *vbox, *hbox, *hbox_indent,
	          *title_label,
	          *progress_bar;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);
	priv = progress_info_bar->priv;

	g_return_if_fail (!priv->in_finished);

	/* Add watch */

	watch->receiver = iris_arbiter_receive
	                    (priv->scheduler, watch->port, 
	                     _iris_progress_monitor_handle_message,
	                     watch, NULL);

	priv->watch_list = g_list_append (priv->watch_list, watch);

	/* Add UI for watch */

	hbox_indent = gtk_hbox_new (FALSE, 0);
	hbox = gtk_hbox_new (FALSE, H_SPACING);

	title_label = gtk_label_new (watch->title);
	gtk_misc_set_alignment (GTK_MISC (title_label), 1.0, 0.5);
	format_watch_title (title_label, watch->title, watch->group != NULL);

	progress_bar = gtk_progress_bar_new ();

	gtk_box_pack_start (GTK_BOX (hbox), title_label, FALSE, TRUE, 0);

	if (watch->group == NULL) {
		gtk_box_pack_start (GTK_BOX (hbox), progress_bar, FALSE, TRUE, 0);

		gtk_box_pack_start (GTK_BOX (hbox_indent), hbox, FALSE, TRUE, EXPANDER_INDENT);
		gtk_box_pack_start (GTK_BOX (priv->box), hbox_indent, FALSE, TRUE, 0);
	}
	else {
		gtk_box_pack_start (GTK_BOX (hbox), progress_bar, TRUE, TRUE, 0);

		gtk_size_group_add_widget (GTK_SIZE_GROUP (watch->group->user_data1),
		                           title_label);

		hbox_indent = gtk_hbox_new (FALSE, 0);
		gtk_box_pack_start (GTK_BOX (hbox_indent), hbox, TRUE, TRUE,
		                    WATCH_INDENT + EXPANDER_INDENT);

		vbox = watch->group->watch_box;
		gtk_box_pack_start (GTK_BOX (vbox), hbox_indent, FALSE, TRUE, 0);

		if (!watch->group->visible)
			show_group (progress_info_bar, watch->group);

	}

	gtk_widget_show_all (hbox_indent);

	watch->toplevel = hbox_indent;
	watch->title_label = title_label;
	watch->progress_bar = progress_bar;
	watch->cancel_button = NULL;

	if (priv->permanent_mode == TRUE)
		/* Ensure we are visible; quicker just to call than to check */
		gtk_widget_show (GTK_WIDGET (progress_info_bar));
}

static void
iris_progress_info_bar_remove_watch (IrisProgressMonitor *progress_monitor,
                                     IrisProgressWatch   *watch,
                                     gboolean             temporary)
{

	IrisProgressInfoBar        *progress_info_bar;
	IrisProgressInfoBarPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (watch->monitor));

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (watch->monitor);
	priv = progress_info_bar->priv;

	if (watch->group != NULL) {
		gtk_size_group_remove_widget (GTK_SIZE_GROUP (watch->group->user_data1),
		                              watch->title_label);

		if (!temporary) {
			g_warn_if_fail (g_list_length (watch->group->watch_list) > 0);

			if (g_list_length (watch->group->watch_list) == 1)
				hide_group (progress_info_bar, watch->group);
			else if (!watch->cancelled && !temporary)
				watch->group->completed_watches ++;
		}
	}

	gtk_widget_destroy (GTK_WIDGET (watch->toplevel));

	priv->watch_list = g_list_remove (priv->watch_list, watch);

	_iris_progress_watch_free (watch);

	if (!temporary)
		/* If no watches left, hide dialog */
		if (priv->watch_list == NULL)
			finish_info_bar (progress_info_bar);
}

static gboolean
iris_progress_info_bar_is_watching_task (IrisProgressMonitor *progress_monitor,
                                         IrisTask            *task)
{
	return (iris_progress_info_bar_get_watch (progress_monitor, task) != NULL);
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
finish_info_bar (IrisProgressInfoBar *progress_info_bar)
{
	IrisProgressInfoBarPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar));

	priv = progress_info_bar->priv;

	/* Emit IrisProgressMonitor::finished */
	_iris_progress_monitor_finished (IRIS_PROGRESS_MONITOR (progress_info_bar));

	if (priv->permanent_mode) {
		/* Check the 'finished' handler didn't destroy the info bar */
		g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar));

		gtk_widget_hide (GTK_WIDGET (progress_info_bar));
	}
}

static gboolean
watch_delayed_remove (gpointer data)
{
	IrisProgressWatch *watch = data;

	iris_progress_info_bar_remove_watch (watch->monitor, watch, FALSE);

	return FALSE;
}

/* When a watch completes/is cancelled, we check to see if any are still running
 * and if they are all done, we self destruct. */
static void
handle_stopped (IrisProgressMonitor *progress_monitor,
                IrisProgressWatch   *watch)
{
	IrisProgressInfoBarPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	priv = IRIS_PROGRESS_INFO_BAR (progress_monitor)->priv;

	if (priv->watch_hide_delay == 0)
		watch_delayed_remove (watch);
	else if (priv->watch_hide_delay == -1);
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
	char                 progress_text[256];
	IrisProgressInfoBar *info_bar;
	GtkWidget           *progress_bar;
	gdouble              group_fraction;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);

	progress_bar = GTK_WIDGET (watch->progress_bar);
	_iris_progress_monitor_format_watch_progress (progress_monitor, watch,
	                                              progress_text);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar),
	                           progress_text);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
	                               watch->fraction);

	if (watch->group != NULL) {
		progress_bar = GTK_WIDGET (watch->group->progress_bar);
		_iris_progress_monitor_format_group_progress
		  (progress_monitor, watch->group, progress_text, &group_fraction);
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar),
		                           progress_text);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
		                               group_fraction);
	}
};

static void
handle_title (IrisProgressMonitor *progress_monitor,
              IrisProgressWatch   *watch)
{
	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	format_watch_title (watch->title_label, watch->title, watch->group != NULL);
}


static void
iris_progress_info_bar_handle_message (IrisProgressMonitor *progress_monitor,
                                       IrisProgressWatch   *watch,
                                       IrisMessage         *message)
{
	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR(progress_monitor));

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
 * iris_progress_info_bar_new:
 *
 * Creates a new #IrisProgressInfoBar.
 *
 * Return value: a newly-created #IrisProgressInfoBar widget
 */
GtkWidget *
iris_progress_info_bar_new ()
{
	GtkWidget *progress_info_bar = g_object_new (IRIS_TYPE_PROGRESS_INFO_BAR, NULL);

	return progress_info_bar;
}

static void
iris_progress_info_bar_set_permanent_mode (IrisProgressMonitor *progress_monitor,
                                           gboolean             enable)
{
	IrisProgressInfoBar *progress_info_bar;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);

	progress_info_bar->priv->permanent_mode = enable;
}

static void
iris_progress_info_bar_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                             gint                 milliseconds)
{
	IrisProgressInfoBar *progress_info_bar;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);

	progress_info_bar->priv->watch_hide_delay = milliseconds;
}

/* Private interface member */
IrisProgressWatch *
iris_progress_info_bar_get_watch (IrisProgressMonitor *progress_monitor,
                                  IrisTask            *task)
{
	IrisProgressInfoBar        *progress_info_bar;
	IrisProgressInfoBarPrivate *priv;
	GList                      *node;

	g_return_val_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor), NULL);
	g_return_val_if_fail (IRIS_IS_TASK (task), NULL);

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);

	priv = progress_info_bar->priv;

	node = g_list_find_custom (priv->watch_list, task, find_watch_by_task);

	return node? node->data: NULL;
}

