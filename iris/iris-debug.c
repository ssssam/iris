/* iris-debug.c
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

#include <stdio.h>
#include "iris-debug.h"
#include "iris-thread.h"

#ifdef ENABLE_PROFILING
__thread GTimer  *timer = NULL;
__thread gdouble  last  = 0.0;
#endif

static IrisDebugSection debug = IRIS_DEBUG_SECTION_NONE;

/*
 * Setup the debugging system
 */
void
iris_debug_init (void)
{
	if (g_getenv ("IRIS_DEBUG")) {
		debug = ~IRIS_DEBUG_SECTION_NONE;
	}
	else {
		if (g_getenv ("IRIS_DEBUG_MESSAGE"))
			debug |= IRIS_DEBUG_SECTION_MESSAGE;
		if (g_getenv ("IRIS_DEBUG_PORT"))
			debug |= IRIS_DEBUG_SECTION_PORT;
		if (g_getenv ("IRIS_DEBUG_RECEIVER"))
			debug |= IRIS_DEBUG_SECTION_RECEIVER;
		if (g_getenv ("IRIS_DEBUG_ARBITER"))
			debug |= IRIS_DEBUG_SECTION_ARBITER;
		if (g_getenv ("IRIS_DEBUG_SCHEDULER"))
			debug |= IRIS_DEBUG_SECTION_SCHEDULER;
		if (g_getenv ("IRIS_DEBUG_THREAD"))
			debug |= IRIS_DEBUG_SECTION_THREAD;
		if (g_getenv ("IRIS_DEBUG_TASK"))
			debug |= IRIS_DEBUG_SECTION_TASK;
		if (g_getenv ("IRIS_DEBUG_QUEUE"))
			debug |= IRIS_DEBUG_SECTION_QUEUE;
		if (g_getenv ("IRIS_DEBUG_STACK"))
			debug |= IRIS_DEBUG_SECTION_STACK;
		if (g_getenv ("IRIS_DEBUG_RROBIN"))
			debug |= IRIS_DEBUG_SECTION_RROBIN;
	}

	iris_debug_init_thread ();
}

/*
 * Setup debugging for the calling thread. Timers are kept
 * in thread local storage for accurate times for each thread
 */
void
iris_debug_init_thread (void)
{
#ifdef ENABLE_PROFILING
	last = 0;
	if (debug)
		timer = g_timer_new ();
#endif
}

/*
 * Print debug line for a method with file and line number
 */
void
iris_debug (IrisDebugSection  section,
            const gchar      *file,
            gint              line,
            const gchar      *function)
{
	if (G_UNLIKELY (debug & section)) {
		IrisThread *thread = iris_thread_get ();
#ifdef ENABLE_PROFILING
		gdouble seconds;

		g_return_if_fail (timer != NULL);

		seconds = g_timer_elapsed (timer, NULL);
		g_print ("[Thread=%lx] [%f (%f)] %s:%d (%s)\n",
		         (gulong)thread, seconds, seconds - last,
		         file, line, function);
		last = seconds;
#else
		g_print ("[Thread=%lx] %s:%d (%s)\n",
		         (gulong)thread, file, line, function);
#endif
		fflush (stdout);
	}
}

/*
 * Print a debug line with file and line number and a message
 */
void
iris_debug_message (IrisDebugSection  section,
                    const gchar      *file,
                    gint              line,
                    const gchar      *function,
                    const gchar      *format, ...)
{
	if (G_UNLIKELY (debug & section)) {
		IrisThread *thread = iris_thread_get ();
#ifdef ENABLE_PROFILING
		gdouble seconds;
#endif

		va_list  args;
		gchar   *msg;

		g_return_if_fail (format != NULL);

		va_start (args, format);
		msg = g_strdup_vprintf (format, args);
		va_end (args);

#ifdef ENABLE_PROFILING
		g_return_if_fail (timer != NULL);

		seconds = g_timer_elapsed (timer, NULL);
		g_print ("[Thread=%lx] [%f (%f)] %s:%d (%s) %s\n",
		         (gulong)thread, seconds, seconds - last,
		         file, line, function, msg);
		last = seconds;
#else
		g_print ("[Thread=%lx] %s:%d (%s) %s\n",
		         (gulong)thread, file, line, function, msg);
#endif
		fflush (stdout);

		g_free (msg);
	}
}
