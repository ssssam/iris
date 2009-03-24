/* iris-message.c
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

#include "iris-message.h"

/**
 * SECTION:iris-message
 * @short_description: a generic message representation
 *
 * #IrisMessage is the representation of a generic message that can be passed
 * around within a process.  It is typically delivered to a port which can
 * help provide actions based on the message.  IrisMessage<!-- -->'s contain
 * a set of key/value pairs that describe the message.  The message itself
 * also has a type, which is defined by the public "what" member of the
 * #IrisMessage struct.
 *
 * Since messages can be expensive, they are reference counted. You can
 * control the lifetime of a #IrisMessage using iris_message_ref() and
 * iris_message_unref().
 *
 * You can add key/values to the message using methods such as
 * iris_message_set_string() and iris_message_set_value().  There are helpers
 * for most base types within GLib.  For complex types, use
 * iris_message_set_value() containing a #GValue with the complex type.
 */

static GValue*
_iris_message_value_new (const GValue *src)
{
	GValue *dst = g_slice_new0 (GValue);
	if (G_LIKELY (src)) {
		g_value_init (dst, G_VALUE_TYPE (src));
		g_value_copy (src, dst);
	}
	return dst;
}

static void
_iris_message_value_free (gpointer data)
{
	GValue *value;

	g_return_if_fail (data != NULL);

	value = data;

	if (G_VALUE_TYPE (value) != G_TYPE_INVALID)
		g_value_unset (value);

	g_slice_free (GValue, value);
}

static void
_iris_message_init_items (IrisMessage *message)
{
	if (G_LIKELY (!message->items))
		message->items = g_hash_table_new_full (g_str_hash,
		                                        g_str_equal,
		                                        g_free,
		                                        _iris_message_value_free);
}

static const GValue*
_iris_message_get_value_internal (IrisMessage *message,
                                  const gchar *name)
{
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (message->items != NULL, NULL);

	return g_hash_table_lookup (message->items, name);
}

static void
_iris_message_set_value_internal (IrisMessage *message,
                                  const gchar *name,
                                  GValue      *value)
{
	g_return_if_fail (message != NULL);

	if (!message->items)
		_iris_message_init_items (message);
	g_assert (message->items != NULL);

	g_hash_table_insert (message->items, g_strdup (name), value);
}

static void
_iris_message_destroy (IrisMessage *message)
{
	g_return_if_fail (message != NULL);
	if (message->items) {
		g_hash_table_unref (message->items);
		message->items = NULL;
	}
}

static void
_iris_message_free (IrisMessage *message)
{
	g_slice_free (IrisMessage, message);
}

/**
 * iris_message_new:
 * @what: the message type
 *
 * Creates a new #IrisMessage.  @what can be any constant used within
 * your application that the local or remote application knows how to
 * handle.
 *
 * Return value: the newly created #IrisMessage.
 */
IrisMessage*
iris_message_new (gint what)
{
	IrisMessage *message;

	message = g_slice_new0 (IrisMessage);
	message->what = what;
	message->ref_count = 1;

	return message;
}

/**
 * iris_message_ref:
 * @message: a #IrisMessage
 *
 * Atomically Increases the reference count of @message by one.
 *
 * Return value: the passed #IrisMessage, with the reference count
 *   increased by one.
 */
IrisMessage*
iris_message_ref (IrisMessage *message)
{
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (message->ref_count > 0, NULL);

	g_atomic_int_inc (&message->ref_count);

	return message;
}

/**
 * iris_message_unref:
 * @message: An #IrisMessage
 *
 * Atomically decrease the reference count of an #IrisMessage.  If the
 * reference count reaches zero, the object is destroyed and all its
 * allocated resources are freed.
 */
void
iris_message_unref (IrisMessage *message)
{
	g_return_if_fail (message != NULL);
	g_return_if_fail (message->ref_count > 0);

	if (g_atomic_int_dec_and_test (&message->ref_count)) {
		_iris_message_destroy (message);
		_iris_message_free (message);
	}
}

/**
 * iris_message_copy:
 * @message: An #IrisMessage
 *
 * Copies @message.  If the node contains complex data types then the
 * reference count of the objects are increased.
 *
 * Return value: the copied #IrisMessage.
 */
IrisMessage*
iris_message_copy (IrisMessage *message)
{
	IrisMessage    *dst;
	GHashTableIter  iter;
	gpointer        key, value;
	gpointer        dkey, dvalue;

	g_return_val_if_fail (message != NULL, NULL);

	dst = iris_message_new (message->what);

	if (message->items) {
		_iris_message_init_items (dst);
		g_hash_table_iter_init (&iter, message->items);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			dkey = g_strdup (key);
			dvalue = _iris_message_value_new ((const GValue*)value);
			g_hash_table_insert (dst->items, dkey, dvalue);
		}
	}

	return dst;
}

/**
 * iris_message_get_value:
 * @message: An #IrisMessage
 * @name: the name of the value to retrieve
 * @value: a #GValue to store the result in
 *
 * Copies the value found using @name as the key into the #GValue
 * pointed to by @value.  Remember to unset your value using
 * g_value_unset() when you are done.
 */
void
iris_message_get_value (IrisMessage *message,
                        const gchar *name,
                        GValue      *value)
{
	const GValue *real_value;
	real_value = _iris_message_get_value_internal (message, name);
	if (!real_value)
		g_assert_not_reached ();
	g_value_init (value, G_VALUE_TYPE (real_value));
	g_value_copy (real_value, value);
}

/**
 * iris_message_set_value:
 * @message: An #IrisMessage
 * @name: the name of the key
 * @value: A #GValue containing the new value
 *
 * Updates the value for @key to use the value pointed to by @value.
 */
void
iris_message_set_value (IrisMessage  *message,
                        const gchar  *name,
                        const GValue *value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = _iris_message_value_new (value);
	_iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_string:
 * @message: An #IrisMessage
 * @name: The key of the item
 *
 * Retrieves the value for @key which must be a string.
 *
 * Return value: a string containing the value for @key.
 */
const gchar*
iris_message_get_string (IrisMessage *message,
                         const gchar *name)
{
	const GValue *value;
	value = _iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, NULL);
	g_return_val_if_fail (G_VALUE_TYPE (value) == G_TYPE_STRING, NULL);
	return g_value_get_string (value);
}

/**
 * iris_message_set_string:
 * @message: An #IrisMessage
 * @name: the key
 * @value: a string
 *
 * Updates the @key for @message to the string pointed to by @value.  The
 * contents of the string is duplicated and stored within the message.
 */
void
iris_message_set_string (IrisMessage *message,
                         const gchar *name,
                         const gchar *value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = _iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_STRING);
	g_value_set_string (real_value, value);

	_iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_int:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.  The key/value pair stored must be an int.
 *
 * Return value: the value for @key as a #gint.
 */
gint
iris_message_get_int (IrisMessage *message,
                      const gchar *name)
{
	const GValue *value;
	value = _iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_int (value);
}

/**
 * iris_message_set_int:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_int (IrisMessage *message,
                      const gchar *name,
                      gint         value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = _iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_INT);
	g_value_set_int (real_value, value);

	_iris_message_set_value_internal (message, name, real_value);
}
