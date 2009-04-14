/* iris-util.h
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

#include <glib.h>

/**
 * g_time_val_compare:
 * @tv1: A pointer to a #GTimeVal
 * @tv2: A pointer to a #GTimeVal
 *
 * Compare two #GTimeVal and return -1, 0, or 1,
 * depending on whether the first is less than, equal to, or greater
 * than, the last.
 *
 * Return value: -1, 0, or 1
 */
inline gint
g_time_val_compare (GTimeVal *tv1,
                    GTimeVal *tv2)
{
	/* Licensed under LGPL-2.1
	 * Evan Nemerson <evan@polussystems.com>
	 * http://github.com/nemequ/vendy/blob/e59a392a53050f00b9b175552eb95c8fde2277a9/vendy/Vendy.Util.vala
	 */
	if (tv1->tv_sec < tv2->tv_sec)
		return -1;
	else if ((tv1->tv_sec > tv2->tv_sec) || (tv1->tv_usec > tv2->tv_usec))
		return 1;
	return (tv1->tv_usec == tv2->tv_usec) ? 0 : -1;
}

/**
 * g_time_val_usec_until:
 * @tv: A #GTimeVal
 *
 * Calculates the number of microseconds until the specified time.
 * If the time is before the current time, 0 is returned.
 *
 * Return value: the number of usecs until @tv has elapsed or 0.
 */
glong
g_time_val_usec_until (GTimeVal *tv)
{
	GTimeVal current = {0,0};
	glong    seconds = 0;
	glong    usec    = 0;

	g_get_current_time (&current);

	if (g_time_val_compare (&current, tv) >= 0)
		return 0;

	seconds = tv->tv_sec - current.tv_sec;

	if (tv->tv_usec > current.tv_usec) {
		usec = tv->tv_usec - current.tv_usec;
	}
	else {
		seconds -= 1;
		usec = G_USEC_PER_SEC - (current.tv_usec - tv->tv_usec);
	}

	return (seconds * G_USEC_PER_SEC) + usec;
}
