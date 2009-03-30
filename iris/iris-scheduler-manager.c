/* iris-scheduler-manager.c
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

#include "iris-scheduler-manager.h"

/* This is going to completely change I think.  The better way to
 * do this is to have the threads do a try_get for their next work
 * item with a timeout.  If they don't get an item within that time,
 * they will call the manager to take them back and rebalance.
 */

#define TIMEOUT_THRESHOLD (G_USEC_PER_SEC * 5) /* 5 seconds */

static gboolean prepare_func  (GSource *source, gint *timeout_);
static gboolean check_func    (GSource *source);
static gboolean dispatch_func (GSource *source, GSourceFunc callback, gpointer user_data);

GSourceFuncs source_funcs = {
	prepare_func,
	check_func,
	dispatch_func,
	NULL,
	NULL,
	NULL
};

typedef struct
{
	GSource   source;
	GTimeVal  last_check;
	GCallback callback;
} IrisGSource;

static gint
g_time_val_compare (GTimeVal *tv1,
                    GTimeVal *tv2)
{
	if (tv1->tv_sec < tv2->tv_sec)
		return -1;
	else if ((tv1->tv_sec > tv2->tv_sec) || (tv1->tv_usec > tv2->tv_usec))
		return 1;
	else
		return (tv1->tv_usec == tv2->tv_usec) ? 0 : -1;
}

/**
 * iris_scheduler_manager_init:
 * @context: A #GMainContext
 * @use_main: Use the main-context for watching schedulers.
 * @callback: An optional #GCallback to be called during re-balancing.
 *
 * The scheduler manager is responsible for watching the scheduler
 * instances so that they may have the proper number of threads
 * based on their current workload.
 *
 * If @context is not %NULL and @use_main is %TRUE<!-- -->, then we will
 * hook into the main loop as a GSource to watch our schedulers for load
 * imbalances.
 *
 * If we are not to use the main loop then we create a thread that is
 * dedicated to monitoring the schedulers.
 */
void
iris_scheduler_manager_init (GMainContext *context,
                             gboolean      use_main,
                             GCallback     callback)
{
	GSource *source;

	if (use_main) {
		if (!context)
			context = g_main_context_default ();
		source = g_source_new (&source_funcs, sizeof (IrisGSource));
		((IrisGSource*)source)->callback = callback;
		g_source_attach (source, context);
	}
	else {
	}
}

static gboolean
prepare_func  (GSource *source,
               gint    *timeout_)
{
	/* We will only rebalance as often as the main loop wakes up,
	 * which should be fine in most cases.  This is because it is
	 * quite likely that there is main loop activity if there are
	 * many threads active.
	 */
	return FALSE;
}

static gboolean
check_func (GSource *source)
{
	IrisGSource *iris_source = (IrisGSource*)source;
	GTimeVal     tv;

	g_get_current_time (&tv);
	g_time_val_add (&tv, -TIMEOUT_THRESHOLD);

	if (g_time_val_compare (&tv, &iris_source->last_check) >= 0)
		return TRUE;

	return TRUE;
}

static gboolean
dispatch_func (GSource     *source,
               GSourceFunc  callback,
               gpointer     user_data)
{
	IrisGSource *iris_source = (IrisGSource*)source;

	if (iris_source->callback)
		iris_source->callback ();

	return FALSE;
}
