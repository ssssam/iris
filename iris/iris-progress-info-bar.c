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
 * or bottom of an application window. By default it shows a single-line
 * #GtkProgressBar representing the overall progress of the tasks it is
 * watching, to save space, but there is a #GtkExpander containing the
 * individual progress bars. Use the #IrisProgressMonitor interface to control
 * it.
 */

static void     iris_progress_info_bar_class_init          (IrisProgressInfoBarClass *progress_info_bar_class);
static void     iris_progress_info_bar_init                (IrisProgressInfoBar *progress_info_bar);
static void     iris_progress_monitor_interface_init       (IrisProgressMonitorInterface *interface);

static GObject *iris_progress_info_bar_constructor         (GType type,
                                                            guint n_construct_properties,
                                                            GObjectConstructParam *construct_params);
static void     iris_progress_info_bar_finalize            (GObject *object);

static void     iris_progress_info_bar_add_watch            (IrisProgressMonitor *progress_monitor,
                                                             IrisProgressWatch   *watch);
static void     iris_progress_info_bar_handle_message       (IrisProgressMonitor *progress_monitor,
                                                             IrisProgressWatch   *watch,
                                                             IrisMessage         *message);

static gboolean iris_progress_info_bar_is_watching_task     (IrisProgressMonitor *progress_monitor,
                                                             IrisTask            *task);

static void     iris_progress_info_bar_set_permanent_mode   (IrisProgressMonitor *progress_monitor,
                                                             gboolean             enable);
static void     iris_progress_info_bar_set_watch_hide_delay (IrisProgressMonitor *progress_monitor,
                                                             int                  milliseconds);

G_DEFINE_TYPE_WITH_CODE (IrisProgressInfoBar, iris_progress_info_bar, GTK_TYPE_INFO_BAR,
                         G_IMPLEMENT_INTERFACE (IRIS_TYPE_PROGRESS_MONITOR,
                                                iris_progress_monitor_interface_init))

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

	priv->in_finished = FALSE;
	priv->permanent_mode = FALSE;

	priv->watch_hide_delay = 500;
}

static void
iris_progress_info_bar_finalize (GObject *object)
{
	GList *node;
	IrisProgressInfoBar        *info_bar = IRIS_PROGRESS_INFO_BAR (object);
	IrisProgressInfoBarPrivate *priv   = info_bar->priv;

	for (node=priv->watch_list; node; node=node->next) {
		IrisProgressWatch *watch = node->data;

		if (watch->finish_timeout_id != 0)
			g_source_remove (watch->finish_timeout_id);

		iris_port_set_receiver (watch->port, NULL);

		g_warn_if_fail (G_OBJECT (watch->receiver)->ref_count == 1);

		g_object_unref (watch->receiver);

		_iris_progress_watch_free (node->data);
	}

	G_OBJECT_CLASS(iris_progress_info_bar_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_watch            = iris_progress_info_bar_add_watch;
	interface->handle_message       = iris_progress_info_bar_handle_message;
	interface->is_watching_task     = iris_progress_info_bar_is_watching_task;
	interface->set_permanent_mode   = iris_progress_info_bar_set_permanent_mode;
	interface->set_watch_hide_delay = iris_progress_info_bar_set_watch_hide_delay;
}

static GObject *
iris_progress_info_bar_constructor (GType type,
                                    guint n_construct_properties,
                                    GObjectConstructParam *construct_params)
{
	GtkWidget  *content_area,
	           *expander, *hbox;

	GObject    *object = ((GObjectClass *) iris_progress_info_bar_parent_class)->constructor
	                      (type, n_construct_properties, construct_params);
	GtkInfoBar *info_bar = GTK_INFO_BAR (object);

	IrisProgressInfoBar *progress_info_bar = IRIS_PROGRESS_INFO_BAR (info_bar);
	IrisProgressInfoBarPrivate *priv = progress_info_bar->priv;

	/* The info bar shows a one-line "overall process" by default, which can
	 * expand to show individual processes.
	 */
	expander = gtk_expander_new (NULL);
	hbox     = gtk_hbox_new(FALSE, 0);

	priv->title_label = gtk_label_new (NULL);

	priv->total_progress_bar = gtk_progress_bar_new ();

	gtk_box_pack_start (GTK_BOX (hbox), priv->title_label, TRUE, FALSE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), priv->total_progress_bar, TRUE, TRUE, 4);
	gtk_expander_set_label_widget (GTK_EXPANDER (expander), hbox);

	priv->watch_vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (expander), priv->watch_vbox);

	gtk_widget_show_all (expander);

	content_area = gtk_info_bar_get_content_area (info_bar);
	gtk_container_add (GTK_CONTAINER (content_area), expander);

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
iris_progress_info_bar_add_watch (IrisProgressMonitor *progress_monitor,
                                  IrisProgressWatch   *watch)
{
	IrisProgressInfoBar        *progress_info_bar;
	IrisProgressInfoBarPrivate *priv;

	GtkWidget *hbox,
	          *indent,
	          *title_label,
	          *progress_bar,
	          *progress_label;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);
	priv = progress_info_bar->priv;

	/* Add watch */

	watch->receiver = iris_arbiter_receive
	                    (priv->scheduler, watch->port, 
	                     _iris_progress_monitor_handle_message,
	                     watch, NULL);

	priv->watch_list = g_list_append (priv->watch_list, watch);

	/* Add UI for watch */

	hbox = gtk_hbox_new (FALSE, 0);
	indent = gtk_label_new ("    ");

	title_label = gtk_label_new (watch->title);
	gtk_misc_set_alignment (GTK_MISC (title_label), 1.0, 0.5);

	progress_bar = gtk_progress_bar_new ();

	progress_label = gtk_label_new (_("Preparing"));
	gtk_misc_set_alignment (GTK_MISC (progress_label), 0.0, 0.5);

	gtk_box_pack_start (GTK_BOX (hbox), indent, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), title_label, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), progress_bar, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (hbox), progress_label, FALSE, TRUE, 4);

	gtk_box_pack_start (GTK_BOX (priv->watch_vbox), hbox, FALSE, TRUE, 4);

	gtk_widget_show_all (hbox);

	watch->container = hbox;
	watch->progress_bar = progress_bar;
	watch->progress_label = progress_label;
	watch->title_label = title_label;
	watch->cancel_button = NULL;

	if (priv->permanent_mode == TRUE)
		/* Ensure we are visible; quicker just to call than to check */
		gtk_widget_show (GTK_WIDGET (progress_info_bar));
}

static gboolean
iris_progress_info_bar_is_watching_task (IrisProgressMonitor *progress_monitor,
                                         IrisTask            *task)
{
	IrisProgressInfoBarPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor), FALSE);

	priv = IRIS_PROGRESS_INFO_BAR (progress_monitor)->priv;

	return (_iris_progress_info_bar_get_watch (IRIS_PROGRESS_INFO_BAR (progress_monitor),
	                                           task) != NULL);
}

/* Private, for tests */
IrisProgressWatch *
_iris_progress_info_bar_get_watch (IrisProgressInfoBar *progress_info_bar,
                                   IrisTask            *task)
{
	IrisProgressInfoBarPrivate *priv;
	GList                      *node;

	g_return_val_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_info_bar), NULL);

	priv = progress_info_bar->priv;

	node = g_list_find_custom (priv->watch_list, task, find_watch_by_task);

	return node? node->data: NULL;
}

/**************************************************************************
 *                        Message processing                              *
 *************************************************************************/

static void
info_bar_finish (IrisProgressInfoBar *progress_info_bar)
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

/* Called by watch hide timeout */
static gboolean
watch_delayed_finish (gpointer data)
{
	IrisProgressWatch          *watch = data;
	IrisProgressInfoBar        *progress_info_bar;
	IrisProgressInfoBarPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROGRESS_INFO_BAR (watch->monitor), FALSE);

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (watch->monitor);
	priv = progress_info_bar->priv;

	/* Does this remove it from the box safely? */
	gtk_widget_destroy (GTK_WIDGET (watch->container));

	/* Remove self from watch list */
	priv->watch_list = g_list_remove (priv->watch_list, watch);

	/* If no watches left, hide dialog */
	if (priv->watch_list == NULL)
		info_bar_finish (progress_info_bar);

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
update_total_progress (IrisProgressInfoBar *info_bar)
{
	GList *node;
	int    count;
	float  fraction;
	IrisProgressInfoBarPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (info_bar));

	priv = info_bar->priv;

	count = 0;
	fraction = 0.0;
	for (node = priv->watch_list; node; node = node->next) {
		IrisProgressWatch *watch = node->data;
		fraction += watch->fraction;
		count ++;
	}

	fraction /= (float)count;

	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->total_progress_bar),
	                               fraction);
}

static void
handle_update (IrisProgressMonitor *progress_monitor,
               IrisProgressWatch   *watch)
{
	char                 progress_text[256];
	IrisProgressInfoBar *info_bar;
	GtkWidget           *progress_bar, *progress_label;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);

	progress_bar = GTK_WIDGET (watch->progress_bar);
	progress_label = GTK_WIDGET (watch->progress_label);

	_iris_progress_monitor_format_watch (progress_monitor, watch,
	                                     progress_text);

	gtk_label_set_text (GTK_LABEL (progress_label), progress_text);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar),
	                               watch->fraction);

	update_total_progress (info_bar);
};

static void
handle_title (IrisProgressMonitor *progress_monitor,
              IrisProgressWatch   *watch)
{
	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	gtk_label_set_text (GTK_LABEL (watch->title_label), watch->title);
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
 * Creates a new #IrisProgressInfoBar. If only one process is being watched,
 * its progress is shown in the info bar. If more than one is being watched,
 * an overall progress bar will be shown with an expander hiding the
 * individual processes.
 *
 * Return value: a newly-created #IrisProgressInfoBar widget
 */
GtkWidget *
iris_progress_info_bar_new ()
{
	GtkWidget *progress_info_bar = g_object_new (IRIS_TYPE_PROGRESS_INFO_BAR, NULL);

	return progress_info_bar;
}

/*static void
iris_progress_info_bar_set_title (IrisProgressMonitor *progress_monitor,
                                  const gchar         *title)
{
	gchar                   *title_format;
	GtkWidget               *title_label;
	IrisProgressInfoBar     *info_bar;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);
	title_label = info_bar->priv->title_label;

	if (title == NULL)
		gtk_label_set_text (GTK_LABEL (title_label), NULL);
		* FIXME: is it necessary to remove/add the label from the box
		 * when it is null?? *
	else {
		title_format = g_strdup_printf ("<b>%s</b>:", title);
		gtk_label_set_markup (GTK_LABEL (title_label), title_format);
		gtk_label_set_use_markup (GTK_LABEL (title_label), TRUE);
		gtk_misc_set_alignment (GTK_MISC (title_label), 0.0, 0.5);
		g_free (title_format);
	}
}
*/
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
