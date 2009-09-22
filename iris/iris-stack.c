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

#include "iris-free-list.h"
#include "iris-stack.h"
#include "gstamppointer.h"

/**
 * SECTION:iris-stack
 * @short_description: A lock-free stack data structure
 *
 * #IrisStack is a lock-free stack implementation.  It is not currently
 * guaranteed to be fully correct.  This is due to the classical ABA
 * problem with many lock-free data structures.  For more information
 * on ABA, see the wikipedia page at
 * <ulink url="http://en.wikipedia.org/wiki/ABA_problem">ABA_problem</ulink>.
 *
 * We do try to greatly reduce the potential of this happening.  This is done
 * by aligning the destination of our internal pointers to sizeof(void*)
 * which leaves us the two lower bits to repurpose as a counter stamp.  This
 * makes the pointer different on every compare-and-swap operation allowing
 * us to fail and retry the operation.  As you might have guessed, if the
 * ABA problem happens 4 times within the pre-emption time of the first
 * thread, the problem can still exist.
 *
 * You can typically solve this with an indirection node.
 */

static void iris_stack_free (IrisStack *stack);

struct _IrisStack
{
	/*< private >*/
	IrisLink      *head;
	IrisFreeList  *free_list;
	volatile gint  ref_count;
};

GType
iris_stack_get_type (void)
{
	static GType stack_type = 0;
	if (G_UNLIKELY (!stack_type))
		stack_type = g_boxed_type_register_static (
				"IrisStack",
				(GBoxedCopyFunc)iris_stack_ref,
				(GBoxedFreeFunc)iris_stack_unref);
	return stack_type;
}

/**
 * iris_stack_ref:
 * @stack: An #IrisStack
 *
 * Atomically increases the reference count of @stack by one.
 *
 * Return value: the passed #IrisStack, which the reference count
 *   increased by one.
 */
IrisStack*
iris_stack_ref (IrisStack *stack)
{
	g_return_val_if_fail (stack != NULL, NULL);
	g_return_val_if_fail (stack->ref_count > 0, NULL);

	g_atomic_int_inc (&stack->ref_count);

	return stack;
}

/**
 * iris_stack_unref:
 * @stack: An #IrisStack
 *
 * Atomically decreases the reference count of @stack.  If the reference
 * count reaches zero, the object is destroyed and all its allocated
 * resources are freed.
 */
void
iris_stack_unref (IrisStack *stack)
{
	g_return_if_fail (stack != NULL);
	g_return_if_fail (stack->ref_count > 0);

	if (g_atomic_int_dec_and_test (&stack->ref_count))
		iris_stack_free (stack);
}

/**
 * iris_stack_new:
 *
 * Creates a new instance of an #IrisStack, which is a concurrent,
 * lock-free stack implementation.
 *
 * Return value: the newly created #IrisStack instance.
 */
IrisStack*
iris_stack_new (void)
{
	IrisStack *stack;

	stack = g_slice_new0 (IrisStack);
	stack->head = g_slice_new0 (IrisLink);
	stack->free_list = iris_free_list_new ();
	stack->ref_count = 1;

	return stack;
}

/**
 * iris_stack_push:
 * @stack: An #IrisStack
 * @data: a pointer
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

static void
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
