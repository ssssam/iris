/* gdestructiblepointer.h
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

#ifndef __G_DESTRUCTIBLE_POINTER_H__
#define __G_DESTRUCTIBLE_POINTER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define G_TYPE_DESTRUCTIBLE_POINTER                  (g_destructible_pointer_get_type())

/**
 * G_VALUE_HOLDS_DESTRUCTIBLE_POINTER:
 * @value: a valid #GValue structure
 *
 * Checks whether the given #GValue can hold values of type
 * %G_TYPE_DESTRUCTIBLE_POINTER.
 *
 * Returns: %TRUE on success.
 */
#define G_VALUE_HOLDS_DESTRUCTIBLE_POINTER(value)     (G_TYPE_CHECK_VALUE_TYPE ((value), G_TYPE_DESTRUCTIBLE_POINTER))

GType    g_destructible_pointer_get_type  ();

void     g_value_set_destructible_pointer (GValue *value, gpointer pointer, GDestroyNotify  destroy_notify);
gpointer g_value_get_destructible_pointer (const GValue *value);

G_END_DECLS

#endif