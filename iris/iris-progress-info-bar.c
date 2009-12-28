/* iris-progress-info-bar.c
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
#include "iris-progress-info-bar.h"
#include "iris-progress-info-bar-private.h"

/**
 * SECTION:iris-progress-info_bar
 * @title: IrisProgressInfoBar
 * @short_description: InfoBar showing progress through tasks or processes.
 *
 * #IrisProgressInfoBar 
 * FIXME: Write
 */

static void     iris_progress_info_bar_class_init      (IrisProgressInfoBarClass *progress_info_bar_class);
static void     iris_progress_info_bar_init            (IrisProgressInfoBar *progress_info_bar);
static void     iris_progress_monitor_interface_init   (IrisProgressMonitorInterface *interface);

static GObject *iris_progress_info_bar_constructor     (GType type,
                                                        guint n_construct_properties,
                                                        GObjectConstructParam *construct_params);
static void     iris_progress_info_bar_finalize        (GObject *object);

static void     iris_progress_info_bar_add_watch       (IrisProgressMonitor *progress_monitor,
                                                        IrisProgressWatch   *watch);
static void     iris_progress_info_bar_update_watch    (IrisProgressMonitor *progress_monitor,
                                                        IrisProgressWatch   *watch);
static void     iris_progress_info_bar_watch_stopped   (IrisProgressMonitor *progress_monitor);

static gboolean iris_progress_info_bar_is_watching_process (IrisProgressMonitor *progress_monitor,
                                                            IrisProcess         *process);

static void     iris_progress_info_bar_set_title       (IrisProgressMonitor *progress_monitor,
                                                        const gchar         *title);
static void     iris_progress_info_bar_set_close_delay (IrisProgressMonitor *progress_monitor,
                                                        gint                 seconds);

static void     iris_progress_info_bar_response        (GtkInfoBar           *info_bar, 
                                                        int                  response_id,
                                                        gpointer             user_data);
 
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

	priv->watch_list = NULL;

	priv->completed = FALSE;

	priv->close_delay = 500;
}

static void
iris_progress_info_bar_finalize (GObject *object)
{
	GList *node;
	IrisProgressInfoBar        *info_bar = IRIS_PROGRESS_INFO_BAR (object);
	IrisProgressInfoBarPrivate *priv   = info_bar->priv;

	for (node=priv->watch_list; node; node=node->next)
		_iris_progress_watch_free (node->data);

	G_OBJECT_CLASS(iris_progress_info_bar_parent_class)->finalize (object);
}

static void
iris_progress_monitor_interface_init (IrisProgressMonitorInterface *interface)
{
	interface->add_watch           = iris_progress_info_bar_add_watch;
	interface->update_watch        = iris_progress_info_bar_update_watch;
	interface->watch_stopped       = iris_progress_info_bar_watch_stopped;
	interface->is_watching_process = iris_progress_info_bar_is_watching_process;
	interface->set_title           = iris_progress_info_bar_set_title;
	interface->set_close_delay     = iris_progress_info_bar_set_close_delay;
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

	priv->watch_table = gtk_table_new (0, 4, FALSE);
	gtk_container_add (GTK_CONTAINER (expander), priv->watch_table);

	gtk_widget_show_all (expander);

	content_area = gtk_info_bar_get_content_area (info_bar);
	gtk_container_add (GTK_CONTAINER (content_area), expander);

	/* FIXME: GTK_MESSAGE_PROGRESS ?? */
	gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar),
	                               GTK_MESSAGE_INFO);

	priv->button = gtk_info_bar_add_button (info_bar, GTK_STOCK_CANCEL,
	                                        GTK_RESPONSE_CANCEL);

	g_signal_connect_swapped (info_bar, "response",
	                          G_CALLBACK (iris_progress_info_bar_response),
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
iris_progress_info_bar_add_watch (IrisProgressMonitor *progress_monitor,
                                  IrisProgressWatch   *watch)
{
	IrisProgressInfoBar        *progress_info_bar;
	IrisProgressInfoBarPrivate *priv;

	int table_rows, table_columns,
	    row_n;

	GtkWidget *indent,
	          *title_label,
	          *progress_bar,
	          *progress_label;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);
	priv = progress_info_bar->priv;

	priv->watch_list = g_list_append (priv->watch_list, watch);

	g_object_get (GTK_TABLE (priv->watch_table),
	              "n-rows", &table_rows,
	              "n-columns", &table_columns,
	              NULL);

	row_n = table_rows;

	gtk_table_resize (GTK_TABLE (priv->watch_table),
	                  ++ table_rows, table_columns);

	indent = gtk_label_new ("    ");

	title_label = gtk_label_new (watch->title);
	gtk_misc_set_alignment (GTK_MISC (title_label), 1.0, 0.5);

	progress_bar = gtk_progress_bar_new ();

	progress_label = gtk_label_new (_("Preparing"));
	gtk_misc_set_alignment (GTK_MISC (progress_label), 0.0, 0.5);

	gtk_table_attach (GTK_TABLE (priv->watch_table), indent,
	                  0, 1, row_n, row_n + 1, GTK_FILL, GTK_FILL, 4, 4);
	gtk_table_attach (GTK_TABLE (priv->watch_table), title_label,
	                  1, 2, row_n, row_n + 1, GTK_FILL, GTK_FILL, 4, 4);
	gtk_table_attach (GTK_TABLE (priv->watch_table), progress_bar,
	                  2, 3, row_n, row_n + 1, 
	                  GTK_EXPAND | GTK_FILL, GTK_FILL, 4, 4);
	gtk_table_attach (GTK_TABLE (priv->watch_table), progress_label,
	                  3, 4, row_n, row_n + 1, GTK_FILL, GTK_FILL, 4, 4);

	gtk_widget_show_all (priv->watch_table);

	watch->user_data = progress_bar;
	watch->user_data2 = progress_label;
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

static gboolean
iris_progress_info_bar_is_watching_process (IrisProgressMonitor *progress_monitor,
                                            IrisProcess         *process)
{
	IrisProgressInfoBarPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor), FALSE);

	priv = IRIS_PROGRESS_INFO_BAR (progress_monitor)->priv;

	return (g_list_find_custom (priv->watch_list, process,
	                            find_watch_by_process)     != NULL);
}

static void
iris_progress_info_bar_update_watch (IrisProgressMonitor *progress_monitor,
                                     IrisProgressWatch   *watch)
{
	char  progress_text[256];
	IrisProgressInfoBar *info_bar;
	GtkWidget           *progress_bar, *progress_label;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);

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

	update_total_progress (info_bar);
};

static gboolean
_delayed_close (gpointer data)
{
	GtkWidget *info_bar = GTK_WIDGET (data);

	gtk_widget_destroy (info_bar);

	return FALSE;
}

/* When a watch completes/is cancelled, we check to see if any are still running
 * and if they are all done, we self destruct. */
void
iris_progress_info_bar_watch_stopped (IrisProgressMonitor *progress_monitor)
{
	IrisProgressInfoBarPrivate *priv;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	priv = IRIS_PROGRESS_INFO_BAR (progress_monitor)->priv;

	/* If any watches are still running, keep working .. */
	if (!_iris_progress_monitor_watch_list_finished (priv->watch_list))
		return;

	/* Finished. We can delay the close to give the display time to update,
	 * and also so we can disable it for eg. tests
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
iris_progress_info_bar_new (const gchar *title)
{
	GtkWidget *progress_info_bar = g_object_new (IRIS_TYPE_PROGRESS_INFO_BAR, NULL);

	iris_progress_info_bar_set_title (IRIS_PROGRESS_MONITOR (progress_info_bar),
	                                  title);

	return progress_info_bar;
}

static void
iris_progress_info_bar_set_title (IrisProgressMonitor *progress_monitor,
                                  const gchar         *title)
{
	gchar                   *title_format;
	GtkWidget               *title_label;
	IrisProgressInfoBar     *info_bar;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);
	title_label = info_bar->priv->title_label;

	title_format = g_strdup_printf ("<b>%s</b>:", title);
	gtk_label_set_markup (GTK_LABEL (title_label), title_format);
	gtk_label_set_use_markup (GTK_LABEL (title_label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (title_label), 0.0, 0.5);
	g_free (title_format);
}

static void
iris_progress_info_bar_set_close_delay (IrisProgressMonitor *progress_monitor,
                                        gint                 milliseconds)
{
	IrisProgressInfoBar *progress_info_bar;

	g_return_if_fail (IRIS_IS_PROGRESS_INFO_BAR (progress_monitor));

	progress_info_bar = IRIS_PROGRESS_INFO_BAR (progress_monitor);

	progress_info_bar->priv->close_delay = milliseconds;
}


/* Handler for cancel button */
static void
iris_progress_info_bar_response (GtkInfoBar *info_bar,
                                 int        response_id,
                                 gpointer   user_data)
{
	IrisProgressInfoBar *progress_info_bar = IRIS_PROGRESS_INFO_BAR (info_bar);
	IrisProgressInfoBarPrivate *priv = progress_info_bar->priv;

	if (priv->completed) {
		/* We're done - the button will have become a close button. */

		if (priv->destroy_timer_id != 0)
			g_source_remove (priv->destroy_timer_id);

		_delayed_close (progress_info_bar);
	} else
		/* watch_stopped will be called for each progress, the info bar will
		 * be closed in this function */
		_iris_progress_monitor_cancel (IRIS_PROGRESS_MONITOR (info_bar),
		                               progress_info_bar->priv->watch_list);
}
