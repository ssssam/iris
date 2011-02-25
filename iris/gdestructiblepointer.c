/* gdestructiblepointer.c
 *
 * Copyright (C) 2009-11 Sam Thursfield <ssssam@gmail.com>
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
#include <glib-object.h>

#include "gdestructiblepointer.h"

/**
 * SECTION:destructible-pointer-type
 * @title: Destructible Pointers
 * @short_description: Store a pointer and a destroy notify in a GValue
 *
 * This mechanism is used internally in #IrisMessage to store a pointer and its
 * destroy notification in a #GValue.
 */

static void
value_init_destructible_pointer (GValue *value)
{
	value->data[0].v_pointer = NULL;
	value->data[1].v_pointer = NULL;
};

static void
value_free_destructible_pointer (GValue *value)
{
	if (value->data[0].v_pointer == NULL)
		/* Could this happen or will this function not be called if the value is unset? */
		return;

	((GDestroyNotify)value->data[1].v_pointer)(value->data[0].v_pointer);
}

static void
value_copy_destructible_pointer (const GValue *src_value,
                                 GValue       *dest_value)
{
	dest_value->data[0].v_pointer = src_value->data[0].v_pointer;
	dest_value->data[1].v_pointer = src_value->data[1].v_pointer;
}

static gpointer
value_peek_pointer0 (const GValue *value)
{
	return value->data[0].v_pointer;
}

GType
g_destructible_pointer_get_type ()
{
	static volatile gsize type_volatile = 0;

	if (g_once_init_enter (&type_volatile)) {
		GType type;

		GTypeInfo info = {
			0,              /* class_size */
			NULL,           /* base_init */
			NULL,           /* base_destroy */
			NULL,           /* class_init */
			NULL,           /* class_destroy */
			NULL,           /* class_data */
			0,              /* instance_size */
			0,              /* n_preallocs */
			NULL,           /* instance_init */
			NULL,           /* value_table */
		};

		static const GTypeValueTable value_table = {
			value_init_destructible_pointer,    /* value_init */
			value_free_destructible_pointer,    /* value_free */
			value_copy_destructible_pointer,    /* value_copy */
			value_peek_pointer0,                /* value_peek_pointer */
			/* "p", */ NULL,                    /* collect_format */
			NULL,                               /* collect_value */
			/* "p", */ NULL,                    /* lcopy_format */
			NULL,                               /* lcopy_value */
		};

		info.value_table = &value_table;

		/* FIXME: should we be fundamental? */
		type = g_type_register_static (G_TYPE_POINTER,
		                               g_intern_static_string ("GDestructiblePointer"),
		                               &info,
		                               0);

		g_once_init_leave (&type_volatile, type);
	}

	return type_volatile;
}

/**
 * g_value_set_destructible_pointer:
 * @value: a valid #GValue of type %G_TYPE_DESTRUCTIBLE_POINTER
 * @pointer: a pointer
 * @destroy_notify: a GDestroyNotify function, called when the value is freed
 *
 * Set the contents of a #GValue to hold @pointer, which will be freed by
 * calling @destroy_notify when the value is freed.
 */
void
g_value_set_destructible_pointer (GValue         *value,
                                  gpointer        pointer,
                                  GDestroyNotify  destroy_notify)
{
	gpointer old_pointer;
	GDestroyNotify old_destroy_notify;

	g_return_if_fail (G_VALUE_HOLDS_DESTRUCTIBLE_POINTER (value));
	g_return_if_fail ((pointer != NULL && destroy_notify != NULL) ||
	                  (pointer == NULL && destroy_notify == NULL));

	old_pointer = value->data[0].v_pointer;
	old_destroy_notify = value->data[1].v_pointer;

	value->data[0].v_pointer = NULL;
	value->data[1].v_pointer = destroy_notify;
	value->data[0].v_pointer = pointer;

	if (old_pointer != NULL)
		old_destroy_notify (old_pointer);
}

/**
 * g_value_get_destructible_pointer:
 * @value: a valid #GValue of type %G_TYPE_DESTRUCTIBLE_POINTER
 *
 * Get the pointer stored in a destructible pointer value.
 *
 * Returns: pointer contents of @value
 */
/* If this got merged in to GLib somehow, it would be best to merge this code
 * with g_value_get_pointer(), because otherwise you'd kind of expect to get
 * a GDestructiblePointer * returned or something ..
 */
gpointer
g_value_get_destructible_pointer (const GValue *value)
{
	g_return_val_if_fail (G_VALUE_HOLDS_DESTRUCTIBLE_POINTER (value), NULL);

	return value->data[0].v_pointer;
}
