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

#include "iris-atomics.h"
#include "iris-rrobin.h"

/**
 * SECTION:iris-rrobin
 * @title: IrisRRobin
 * @short_description: A lock-free round-robin
 *
 * #IrisRRobin is a structure used for implementing round-robin semantics.
 * To use it, call iris_rrobin_append() including the data for each slot
 * in your round-robin.  After that, you may use iris_rrobin_apply() to
 * perform a callback using the next data-slot.
 *
 * #IrisRRobin is completely multithread-safe.
 */

GType
iris_rrobin_get_type (void)
{
	static GType rrobin_type = 0;
	if (G_UNLIKELY (!rrobin_type))
		rrobin_type = g_boxed_type_register_static (
				"IrisRRobin",
				(GBoxedCopyFunc)iris_rrobin_ref,
				(GBoxedFreeFunc)iris_rrobin_unref);
	return rrobin_type;

}

/**
 * iris_rrobin_new:
 * @size: The maximum number of entries
 *
 * Creates a new instance of the lock-free, round-robin data structure.
 *
 * Return value: the new #IrisRRobin instance
 */
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
	rrobin->ref_count = 1;

	return rrobin;
}

static void
iris_rrobin_free (IrisRRobin *rrobin)
{
	g_free (rrobin);
}

/**
 * iris_rrobin_ref:
 * @rrobin: An #IrisRRobin
 *
 * Increments the reference count of @rrobin atomically by one.
 *
 * Return value: The @rrobin instance with its reference count incremented.
 */
IrisRRobin*
iris_rrobin_ref (IrisRRobin *rrobin)
{
	g_return_val_if_fail (rrobin != NULL, NULL);
	g_return_val_if_fail (rrobin->ref_count > 0, NULL);

	g_atomic_int_inc (&rrobin->ref_count);
	return rrobin;
}

/**
 * iris_rrobin_unref:
 * @rrobin: An #IrisRRobin
 *
 * Atomically decreates the reference count of @rrobin. If the reference
 * count reaches zero, the object is destroyed and all its allocated
 * resources are freed.
 */
void
iris_rrobin_unref (IrisRRobin *rrobin)
{
	g_return_if_fail (rrobin != NULL);
	g_return_if_fail (rrobin->ref_count > 0);

	if (g_atomic_int_dec_and_test (&rrobin->ref_count))
		iris_rrobin_free (rrobin);
}

/**
 * iris_rrobin_append:
 * @rrobin: An #IrisRRobin
 * @data: a pointer to data
 *
 * Appends a new data item to the round-robin structure. The data supplied
 * will be added to the arguments of the callback used in iris_rrobin_apply().
 *
 * Return value: %TRUE if there was enough free space to append the item.
 */
gboolean
iris_rrobin_append (IrisRRobin *rrobin,
                    gpointer    data)
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

/**
 * iris_rrobin_remove:
 * @rrobin: An #IrisRRobin
 * @data: a pointer to data
 *
 * Removes the first instance of @data within the @rrobin structure.
 */
void
iris_rrobin_remove (IrisRRobin *rrobin,
                    gpointer    data)
{
	gint     i;
	gboolean ignore;

	g_return_if_fail (data != NULL);

	for (i = 0; i < rrobin->size; i++) {
		if (g_atomic_pointer_compare_and_exchange (&rrobin->data [i], data, NULL)) {
			ignore = g_atomic_int_dec_and_test (&rrobin->count);
			break;
		}
	}
}

/**
 * iris_rrobin_apply:
 * @rrobin: An #IrisRRobin
 * @callback: An #IrisRRobinFunc to execute
 * @user_data: user data supplied to callback
 *
 * Executes @callback using the data from the next item in the round-robin
 * data structure.
 *
 * @callback should normally return %TRUE. If the item passed is not suitable
 * for some reason, it may return %FALSE to request the next item in @rrobin.
 *
 * Return value: %FALSE if the callback rejected every item or the #IrisRRobin
 *               was empty, else %TRUE.
 */
gboolean
iris_rrobin_apply (IrisRRobin     *rrobin,
                   IrisRRobinFunc  callback,
                   gpointer        user_data)
{
	gpointer data;
	gint     my_index;
	gint     first_index = -1;

	gint     count;

	g_return_val_if_fail (rrobin != NULL, FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);

_try_next_index:

	count = g_atomic_int_get (&rrobin->count);
	g_return_val_if_fail (count > 0, FALSE);

	/* get our index to try */
	my_index = iris_atomics_fetch_and_inc (&rrobin->active) % count;

	/* continue to look for an item if there isn't one here.
	 * this should only happen during an item move.
	 */
	if (G_UNLIKELY ((data = rrobin->data [my_index]) == NULL)) {
		if (first_index == -1) {
			first_index = my_index;
		} else
		if (first_index == my_index)
			/* we iterated through the entire list. we might
			 * as well check the current count just to verify
			 * it has an item. if not, we will return false.
			 */
			if (g_atomic_int_get (&rrobin->count) == 0)
				return FALSE;

		goto _try_next_index;
	}

	if (callback (rrobin->data [my_index], user_data) == FALSE) {
		if (first_index == -1) {
			first_index = my_index;
		} else
		if (first_index == my_index)
			/* Looks like the callback has rejected every single item. */
			return FALSE;

		goto _try_next_index;
	}

	return TRUE;
}

/**
 * iris_rrobin_foreach:
 * @rrobin: An #IrisRRobin
 * @callback: An #IrisRRobinForeachFunc
 * @user_data: user data supplied to @callback
 *
 * Executes @callback for each item in the #IrisRRobin structure. If
 * @callback returns %FALSE, then iteration is stopped and the method
 * will return.
 */
void
iris_rrobin_foreach (IrisRRobin            *rrobin,
                     IrisRRobinForeachFunc  callback,
                     gpointer               user_data)
{
	gint end;
	gint i;

	g_return_if_fail (rrobin != NULL);
	g_return_if_fail (callback != NULL);

	end = g_atomic_int_get (&rrobin->count);

	for (i = 0; i < end; i++) {
		if (!rrobin->data [i])
			continue;
		if (!callback (rrobin, rrobin->data [i], user_data))
			break;
	}
}
