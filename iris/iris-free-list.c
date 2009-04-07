/* iris-free-list.c
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

#include "iris-free-list.h"

/**
 * iris_free_list_new:
 *
 * Creates a new instance of #IrisFreeList.
 *
 * Return value: The newly created #IrisFreeList
 */
IrisFreeList*
iris_free_list_new (void)
{
	IrisFreeList *free_list;
	
	free_list = g_slice_new0 (IrisFreeList);
	free_list->head = g_slice_new0 (IrisLink);
	
	return free_list;
}

/**
 * iris_free_list_free:
 * @free_list: An #IrisFreeList
 *
 * Frees the data associated with @free_list.  Unlike the other methods
 * of this data structure, this method is not always going to be thread
 * safe. Obviously you don't want to be accessing it while free'ing the
 * structure.
 */
void
iris_free_list_free (IrisFreeList *free_list)
{
	IrisLink *link, *tmp;
	
	g_return_if_fail (free_list != NULL);
	
_try_swap:
	link = free_list->head;
	if (!g_atomic_pointer_compare_and_exchange ((gpointer*)&free_list->head, link, NULL))
		goto _try_swap;
	
	while (link) {
		tmp = link->next;
		if (link)
			g_slice_free (IrisLink, link);
		link = tmp;
	}
	
	g_slice_free (IrisFreeList, free_list);
}

/**
 * iris_free_list_get:
 * @free_list: An #IrisFreeList
 *
 * Retreives or creates a new #IrisLink instance.  The created link should
 * be returned to the free list using iris_free_list_put().
 *
 * Return value: the previously allocated, or new #IrisLink instance.
 */
IrisLink*
iris_free_list_get (IrisFreeList *free_list)
{
	IrisLink *link;
	
	g_return_val_if_fail (free_list != NULL, NULL);
	
	do {
		link = free_list->head->next;
		if (link == NULL)
			return g_slice_new0 (IrisLink);
	} while (!g_atomic_pointer_compare_and_exchange ((gpointer*)&free_list->head->next, link, link->next));
	
	link->next = NULL;
	
	return link;
}

/**
 * iris_free_list_put:
 * @free_list: An #IrisFreeList
 * @link: An #IrisLink
 *
 * Returns an #IrisLink instance back to the #IrisFreeList instance.
 */
void
iris_free_list_put (IrisFreeList *free_list,
                    IrisLink     *link)
{
	g_return_if_fail (free_list != NULL);
	g_return_if_fail (link != NULL);
	
	link->data = NULL;
	
	do {
		link->next = free_list->head->next;
	} while (!g_atomic_pointer_compare_and_exchange ((gpointer*)&free_list->head->next, link->next, link));
}
