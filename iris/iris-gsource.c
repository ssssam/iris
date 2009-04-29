/* iris-gsource.c
 *
 * Copyright (C) 2009 Christian Hergert <chris@dronelabs.com>
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

#include "iris-gsource.h"
#include "iris-queue.h"

typedef struct
{
	GSource       source;
	GMainContext *context;
	IrisQueue    *queue;
} IrisGSource;

static gboolean
iris_gsource_prepare (GSource *gsource,
                      gint    *timeout)
{
	IrisGSource *source;

	source = (IrisGSource*)gsource;
	*timeout = -1;

	return (iris_queue_length (source->queue) > 0);
}

static gboolean
iris_gsource_check (GSource *gsource)
{
	IrisGSource *source;

	source = (IrisGSource*)gsource;

	return (iris_queue_length (source->queue) > 0);
}

static gboolean
iris_gsource_dispatch (GSource     *source,
                       GSourceFunc  callback,
                       gpointer     user_data)
{
	g_return_val_if_fail (callback != NULL, FALSE);
	return callback (user_data);
}

static void
iris_gsource_finalize (GSource *gsource)
{
	IrisGSource *source;

	source = (IrisGSource*)gsource;

	iris_queue_unref (source->queue);
	source->context = NULL;
}

static GSourceFuncs source_funcs = {
	iris_gsource_prepare,
	iris_gsource_check,
	iris_gsource_dispatch,
	iris_gsource_finalize,
};

guint
iris_gsource_new (IrisQueue    *queue,
                  GMainContext *context,
                  GSourceFunc   callback,
                  gpointer      user_data)
{
	IrisGSource *source;
	GSource     *gsource;
	guint        result;

	g_return_val_if_fail (queue != NULL, 0);
	g_return_val_if_fail (callback != NULL, 0);

	gsource = g_source_new (&source_funcs, sizeof (IrisGSource));
	g_source_set_priority (gsource, G_PRIORITY_DEFAULT_IDLE);
	g_source_set_callback (gsource, callback, user_data, NULL);

	source = (IrisGSource*)gsource;
	source->context = context != NULL? context : g_main_context_default ();
	source->queue = iris_queue_ref (queue);

	result = g_source_attach (gsource, context);
	g_source_unref (gsource);

	return result;
}
