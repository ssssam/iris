/* iris-rrobin.c
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

#include "iris-rrobin.h"

IrisRRobin*
iris_rrobin_new (gint size)
{
	IrisRRobin *rrobin;

	g_return_val_if_fail (size > 0, NULL);

	/* malloc our dynamic sized array */
	rrobin = g_malloc0 (sizeof (IrisRRobin) + ((size - 1) * sizeof (gpointer)));

	if (rrobin == NULL)
		return NULL;

	rrobin->size = size;
	rrobin->count = 0;

	return rrobin;
}

gboolean
iris_rrobin_append (IrisRRobin *rrobin,
                    gpointer  data)
{
	gint count;
	gint i;

	g_return_val_if_fail (rrobin != NULL, FALSE);

_try_append:

	count = g_atomic_int_get (&rrobin->count);

	/* check we are not at capacity */
	if (count + 1 > rrobin->size)
		return FALSE;

	if (!g_atomic_int_compare_and_exchange (&rrobin->count, count, count + 1))
		goto _try_append;

	/* try to find a location to add to from the beginning */
	for (i = 0; i < rrobin->size; i++)
		if (g_atomic_pointer_compare_and_exchange (&rrobin->data [i], NULL, data))
			break;

	return TRUE;
}

void
iris_rrobin_remove (IrisRRobin *rrobin,
                    gpointer  data)
{
	gint i;

	g_return_if_fail (data != NULL);

	for (i = 0; i < rrobin->size; i++)
		if (g_atomic_pointer_compare_and_exchange (&rrobin->data [i], data, NULL))
			break;
}

gboolean
iris_rrobin_apply (IrisRRobin     *rrobin,
                   IrisRRobinFunc  callback,
                   gpointer        user_data)
{
	gpointer data;
	gint     my_index;
	gint     first_index = -1;

	g_return_val_if_fail (rrobin != NULL, FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);

_try_next_index:

	/* get our index to try */
	my_index = __sync_fetch_and_add (&rrobin->active, 1) % rrobin->count;

	/* continue to look for an item if there isn't one here.
	 * this should only happen during an item move.
	 */
	if (G_UNLIKELY ((data = rrobin->data [my_index]) == NULL)) {
		/* keep track if the first item so we know when we
		 * got all the way through the list.
		 */
		if (first_index == -1) {
			first_index = my_index;
		}
		else if (first_index == my_index) {
			/* we iterated through the entire list. we might
			 * as well check the current count just to verify
			 * it has an item. if not, we will return false.
			 */
			if (g_atomic_int_get (&rrobin->count) == 0)
				return FALSE;
		}

		goto _try_next_index;
	}

	callback (rrobin->data [my_index], user_data);

	return TRUE;
}

void
iris_rrobin_free (IrisRRobin *rrobin)
{
	g_free (rrobin);
}
