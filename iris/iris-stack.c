/* iris-stack.c
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

#include "iris-stack.h"
#include "gstamppointer.h"

/**
 * iris_stack_new:
 *
 * Creates a new instance of an #IrisStack, which is a concurrent,
 * lock-free stack implementation.
 */
IrisStack*
iris_stack_new (void)
{
	IrisStack *stack;

	stack = g_slice_new0 (IrisStack);
	stack->head = g_slice_new0 (IrisLink);
	stack->free_list = iris_free_list_new ();

	return stack;
}

/**
 * iris_stack_free:
 * @stack: An #IrisStack
 *
 * Frees the memory associated with the #IrisStack.  This should never
 * be called while other threads are still accessing the structure.
 */
void
iris_stack_free (IrisStack *stack)
{
	IrisLink *link, *tmp;

	g_return_if_fail (stack != NULL);

_try_swap:
	link = stack->head;
	if (!g_atomic_pointer_compare_and_exchange ((gpointer*)&stack->head, link, NULL))
		goto _try_swap;

	link = G_STAMP_POINTER_GET_POINTER (link);

	while (link) {
		tmp = G_STAMP_POINTER_GET_POINTER (link->next);
		if (link)
			g_slice_free (IrisLink, link);
		link = tmp;
	}

	iris_free_list_free (stack->free_list);
	g_slice_free (IrisStack, stack);
}

/**
 * iris_stack_push:
 * @stack: An #IrisStack
 *
 * Pushes a new item onto the stack atomically.
 */
void
iris_stack_push (IrisStack *stack,
                 gpointer   data)
{
	IrisLink *link;

	g_return_if_fail (stack != NULL);

	link = iris_free_list_get (stack->free_list);

	link = G_STAMP_POINTER_INCREMENT (link);
	G_STAMP_POINTER_GET_LINK (link)->data = data;

	do {
		G_STAMP_POINTER_GET_LINK (link)->next = stack->head->next;
	} while (!g_atomic_pointer_compare_and_exchange ((gpointer*)&stack->head->next,
	                                                 G_STAMP_POINTER_GET_LINK (link)->next,
	                                                 link));
}

/**
 * iris_stack_pop:
 * @stack: An #IrisStack
 *
 * Pops an item off of the stack atomically. If no item is on the stack,
 * then %NULL is returned.
 *
 * Return value: the most recent item on the stack, or %NULL
 */
gpointer
iris_stack_pop (IrisStack *stack)
{
	IrisLink *link;
	gpointer  result = NULL;

	g_return_val_if_fail (stack != NULL, NULL);

	do {
		link = stack->head->next;
		if (link == NULL)
			return NULL;
	} while (!g_atomic_pointer_compare_and_exchange ((gpointer*)&stack->head->next,
	                                                 link,
	                                                 G_STAMP_POINTER_GET_LINK (link)->next));

	result = G_STAMP_POINTER_GET_LINK (link)->data;
	iris_free_list_put (stack->free_list, link);

	return result;
}
