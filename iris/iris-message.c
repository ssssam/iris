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

#include <string.h>
#include <gobject/gvaluecollector.h>

#include "gdestructiblepointer.h"
#include "iris-message.h"

/**
 * SECTION:iris-message
 * @title: IrisMessage
 * @short_description: A generic message representation
 *
 * #IrisMessage is the representation of a message that can be passed
 * within a process.  It is typically delivered to a port which can
 * help provide actions based on the message.  IrisMessage<!-- -->'s contain
 * a set of key/value pairs that describe the message.  The message itself
 * also has a type, which is defined by the public "what" member of the
 * #IrisMessage struct.
 *
 * Since messages can be expensive, they are reference counted. This uses the
 * 'floating reference' model of #GInitiallyUnowned. You can control the
 * lifetime of an #IrisMessage using iris_message_ref() and
 * iris_message_unref(). Additionally, when an #IrisMessage is created, it is
 * given a <firstterm>floating</firstterm> reference, which must be removed
 * before the object is finalized using iris_message_ref_sink(). Functions which
 * "swallow" a message, such as iris_port_post() and iris_process_enqueue(),
 * will do this automatically, and the advantage of the floating reference is
 * that you can simply call:
 * [| iris_port_post (iris_message_new (MY_MESSAGE_ID)); |]
 * The floating reference of the new message will be removed automatically once
 * it has been delivered and, because there were no other references added with
 * iris_message_ref(), the message will be freed.
 *
 * You can add key/values to the message using methods such as
 * iris_message_set_string() and iris_message_set_value().  There are helpers
 * for most base types within GLib.  For complex types, use
 * iris_message_set_value() containing a #GValue with the complex type.
 *
 * Named keys uses a hashtable internally which may be more of an
 * expensive operation than is desired.  #IrisMessage provides a way to
 * pack the data into the message using iris_message_set_data().  For
 * light-weight messages containing a single value this is preferred.
 *
 * Updating the structure is not thread-safe.  Generally it is not
 * recommended to modify a message after passing it.
 */

static GValue*
iris_message_value_new (const GValue *src)
{
	GValue *dst = g_slice_new0 (GValue);
	if (G_LIKELY (src)) {
		g_value_init (dst, G_VALUE_TYPE (src));
		g_value_copy (src, dst);
	}
	return dst;
}

static void
iris_message_value_free (gpointer data)
{
	GValue *value;

	g_return_if_fail (data != NULL);

	value = data;

	if (G_VALUE_TYPE (value) != G_TYPE_INVALID)
		g_value_unset (value);

	g_slice_free (GValue, value);
}

static void
iris_message_init_items (IrisMessage *message)
{
	if (G_LIKELY (!message->items))
		message->items = g_hash_table_new_full (g_str_hash,
		                                        g_str_equal,
		                                        g_free,
		                                        iris_message_value_free);
}

static const GValue*
iris_message_get_value_internal (IrisMessage *message,
                                 const gchar *name)
{
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (message->items != NULL, NULL);

	return g_hash_table_lookup (message->items, name);
}

static void
iris_message_set_value_internal (IrisMessage *message,
                                 const gchar *name,
                                 GValue      *value)
{
	g_return_if_fail (message != NULL);

	if (!message->items)
		iris_message_init_items (message);
	g_hash_table_insert (message->items, g_strdup (name), value);
}

static void
iris_message_destroy (IrisMessage *message)
{
	g_return_if_fail (message != NULL);

	if (g_atomic_int_get (&message->floating))
		g_warning ("A message was finalized with the floating reference still "
		           "present. iris_message_ref_sink() must be called before the "
		           "final reference is removed.");

	if (message->items) {
		g_hash_table_unref (message->items);
		message->items = NULL;
	}

	if (G_VALUE_TYPE (&message->data) != G_TYPE_INVALID)
		g_value_unset (&message->data);
}

static void
iris_message_free (IrisMessage *message)
{
	g_slice_free (IrisMessage, message);
}

GType
iris_message_get_type (void)
{
	static GType message_type = 0;

	if (G_UNLIKELY (!message_type))
		message_type = g_boxed_type_register_static ("IrisMessage",
		                                             (GBoxedCopyFunc) iris_message_ref,
		                                             (GBoxedFreeFunc) iris_message_unref);

	return message_type;
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
	message->floating = TRUE;

	return message;
}

/**
 * iris_message_new_data:
 * @what: The message type
 * @type: the #GType of the data element
 * @Varargs: the value of the data element, in the appropriate form for @type
 *
 * Creates a new #IrisMessage instance with the data value initialized.
 * The ellipsis parameter is used so you may pass any type of value or
 * pointer into the constructor, however only one is allowed.
 *
 * Return value: The newly created #IrisMessage instance
 */
IrisMessage*
iris_message_new_data (gint  what,
                       GType type, ...)
{
	IrisMessage *message;
	va_list      args;
	gchar       *error = NULL;
	
	message = iris_message_new (what);

	if (type != G_TYPE_INVALID) {
		g_value_init (&message->data, type);
		va_start (args, type);
		G_VALUE_COLLECT (&message->data, args, 0, &error);
		if (error) {
			g_warning ("%s: %s", G_STRFUNC, error);
			g_free (error);
			g_value_unset (&message->data);
		}
		va_end (args);
	}

	return message;
}

/**
 * iris_message_new_items:
 * @what: the message type
 * @first_name: the name of the first field in the message
 * @...: the GType and value for the first property, followed optionally
 *   by more name/type/value triplets, follwed by %NULL
 *
 * Creates a new instance of a #IrisMessage and sets its fields.
 *
 * Return value: a new instance of #IrisMessage.
 */
IrisMessage*
iris_message_new_items (gint         what,
                       const gchar *first_name,
                       ...)
{
	IrisMessage *message;
	va_list      args;
	const gchar *name;
	GType        g_type;
	GValue       g_value = {0,};
	gchar       *error   = NULL;

	message = iris_message_new (what);

	if (first_name == NULL)
		return message;

	name = first_name;
	va_start (args, first_name);

	while (name != NULL) {
		g_type = va_arg (args, GType);
		g_value_init (&g_value, g_type);
		G_VALUE_COLLECT (&g_value, args, 0, &error);

		if (error) {
			g_warning ("%s: %s", G_STRFUNC, error);
			g_free (error);
			g_value_unset (&g_value);
			break;
		}

		iris_message_set_value (message, name, &g_value);

		g_value_unset (&g_value);
		name = va_arg (args, const gchar*);
	}

	va_end (args);

	return message;
}

/**
 * iris_message_ref:
 * @message: a #IrisMessage
 *
 * Atomically increases the reference count of @message by one.
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
 * iris_message_ref_sink:
 * @message: a #IrisMessage
 *
 * Increases the reference count of @message by one <emphasis>or</emphasis>
 * "takes ownership" of the floating reference, leaving the actual reference
 * count unchanged.
 *
 * Return value: the passed #IrisMessage.
 */
IrisMessage*
iris_message_ref_sink (IrisMessage *message)
{
	g_return_val_if_fail (message != NULL, NULL);
	g_return_val_if_fail (message->ref_count > 0, NULL);

	if (g_atomic_int_compare_and_exchange (&message->floating, TRUE, FALSE));
		/* We sunk the floating reference */
	else
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
		iris_message_destroy (message);
		iris_message_free (message);
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
		iris_message_init_items (dst);
		g_hash_table_iter_init (&iter, message->items);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			dkey = g_strdup (key);
			dvalue = iris_message_value_new ((const GValue*)value);
			g_hash_table_insert (dst->items, dkey, dvalue);
		}
	}

	return dst;
}

/**
 * iris_message_get_data:
 * @message: An #IrisMessage
 *
 * Retrieves the data value for the #IrisMessage.  A message may have one
 * data value within it that is not associated with a key.  This is that
 * value.
 *
 * Return value: A pointer to a GValue that should not be modified.
 */
G_CONST_RETURN GValue*
iris_message_get_data (IrisMessage *message)
{
	g_return_val_if_fail (message != NULL, NULL);
	return &message->data;
}

/**
 * iris_message_set_data:
 * @message: An #IrisMessage
 * @value: A #GValue
 *
 * Updates the data field for the message. A message may have only one
 * data value within it that is not associated with a key. This is that
 * value.
 */
void
iris_message_set_data (IrisMessage  *message,
                       const GValue *value)
{
	g_return_if_fail (message != NULL);
	g_return_if_fail (value != NULL);

	if (G_VALUE_TYPE (&message->data) != G_TYPE_INVALID)
		g_value_unset (&message->data);

	g_value_init (&message->data, G_VALUE_TYPE (value));
	g_value_copy (value, &message->data);
}

/**
 * iris_message_count_names:
 * @message: An #IrisMessage
 *
 * Retrieves the number of key/value pairs that are currently stored within
 * the message.
 *
 * Return value: the number of key/value pairs
 */
guint
iris_message_count_names (IrisMessage *message)
{
	g_return_val_if_fail (message != NULL, 0);
	if (G_UNLIKELY (!message->items))
		return 0;
	return g_hash_table_size (message->items);
}

/**
 * iris_message_contains:
 * @message: An #IrisMessage
 * @name: the name to lookup
 *
 * Checks to see if @message contains a field named @name.
 *
 * Return value: TRUE if the message contains @name
 */
gboolean
iris_message_contains (IrisMessage *message,
                       const gchar *name)
{
	g_return_val_if_fail (message != NULL, FALSE);
	if (!message->items)
		return FALSE;
	return (NULL != g_hash_table_lookup (message->items, name));
}

/**
 * iris_message_is_empty:
 * @message: An #IrisMessage
 *
 * Checks to see if the message is currently empty, meaning it has no
 * key/value pairs associated.
 *
 * Return value: TRUE if there are no key/value pairs associated.
 */
gboolean
iris_message_is_empty (IrisMessage *message)
{
	g_return_val_if_fail (message != NULL, FALSE);

	if (G_UNLIKELY (!message->items))
		return TRUE;

	return (g_hash_table_size (message->items) == 0);
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

	real_value = iris_message_get_value_internal (message, name);
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

	real_value = iris_message_value_new (value);
	iris_message_set_value_internal (message, name, real_value);
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
	value = iris_message_get_value_internal (message, name);
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

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_STRING);
	g_value_set_string (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_int:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key as a #gint.
 */
gint
iris_message_get_int (IrisMessage *message,
                      const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
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

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_INT);
	g_value_set_int (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_int64:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key as a #gint64.
 */
gint64
iris_message_get_int64 (IrisMessage *message,
                        const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_int64 (value);
}

/**
 * iris_message_set_int64:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_int64 (IrisMessage *message,
                        const gchar *name,
                        gint64       value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_INT64);
	g_value_set_int64 (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_float:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key as a #gfloat.
 */
gfloat
iris_message_get_float (IrisMessage *message,
                        const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_float (value);
}

/**
 * iris_message_set_float:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_float (IrisMessage *message,
                        const gchar *name,
                        gfloat       value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_FLOAT);
	g_value_set_float (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_double:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key as a #gdouble.
 */
gdouble
iris_message_get_double (IrisMessage *message,
                         const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_double (value);
}

/**
 * iris_message_set_double:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_double (IrisMessage *message,
                         const gchar *name,
                         gdouble      value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_DOUBLE);
	g_value_set_double (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_long:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key
 */
glong
iris_message_get_long (IrisMessage *message,
                       const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_long (value);
}

/**
 * iris_message_set_long:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_long (IrisMessage *message,
                       const gchar *name,
                       glong        value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_LONG);
	g_value_set_long (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_ulong:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key
 */
gulong
iris_message_get_ulong (IrisMessage *message,
                       const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_ulong (value);
}

/**
 * iris_message_set_ulong:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_ulong (IrisMessage *message,
                        const gchar *name,
                        gulong       value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_ULONG);
	g_value_set_ulong (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_char:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key
 */
gchar
iris_message_get_char (IrisMessage *message,
                       const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_char (value);
}

/**
 * iris_message_set_char:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_char (IrisMessage *message,
                       const gchar *name,
                       gchar        value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_CHAR);
	g_value_set_char (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_uchar:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key
 */
guchar
iris_message_get_uchar (IrisMessage *message,
                        const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_uchar (value);
}

/**
 * iris_message_set_uchar:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_uchar (IrisMessage *message,
                        const gchar *name,
                        guchar       value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_UCHAR);
	g_value_set_uchar (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_boolean:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key
 */
gboolean
iris_message_get_boolean (IrisMessage *message,
                          const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_boolean (value);
}

/**
 * iris_message_set_boolean:
 * @message: An #IrisMessage
 * @name: the key
 * @value: the value
 *
 * Updates @message to use @value as the value for @key.
 */
void
iris_message_set_boolean (IrisMessage *message,
                          const gchar *name,
                          gboolean     value)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_BOOLEAN);
	g_value_set_boolean (real_value, value);

	iris_message_set_value_internal (message, name, real_value);
}

/**
 * iris_message_get_pointer:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the value for @key.
 *
 * Return value: the value for @key
 */
gpointer
iris_message_get_pointer (IrisMessage *message,
                          const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, 0);
	return g_value_get_pointer (value);
}

/**
 * iris_message_set_pointer:
 * @message: An #IrisMessage
 * @name: the key
 * @pointer: the value
 *
 * Updates @message to use value @pointer as the value for @key.
 */
void
iris_message_set_pointer (IrisMessage *message,
                          const gchar *name,
                          gpointer     pointer)
{
	GValue *value;

	g_return_if_fail (message != NULL);

	value = iris_message_value_new (NULL);
	g_value_init (value, G_TYPE_POINTER);
	g_value_set_pointer (value, pointer);

	iris_message_set_value_internal (message, name, value);
}

/**
 * iris_message_set_pointer_full:
 * @message: An #IrisMessage
 * @name: the key
 * @pointer: the value
 * @destroy_notify: function to call when @message is finalized, that will free
 *                  the data pointed to by @value.
 *
 * Updates @message to use @value as the value for @key, specifying how to free
 * @value when the message is no longer needed.
 */
void
iris_message_set_pointer_full (IrisMessage   *message,
                              const gchar    *name,
                              gpointer        pointer,
                              GDestroyNotify  destroy_notify)
{
	GValue *value;

	g_return_if_fail (message != NULL);

	value = iris_message_value_new (NULL);
	g_value_init (value, G_TYPE_DESTRUCTIBLE_POINTER);
	g_value_set_destructible_pointer (value, pointer, destroy_notify);

	iris_message_set_value_internal (message, name, value);
}

/**
 * iris_message_get_object:
 * @message: An #IrisMessage
 * @name: the key
 *
 * Retrieves the object value for @key.
 *
 * Return value: the value for @key or %NULL
 */
GObject*
iris_message_get_object (IrisMessage *message,
                         const gchar *name)
{
	const GValue *value;
	value = iris_message_get_value_internal (message, name);
	g_return_val_if_fail (value != NULL, NULL);
	return g_value_get_object (value);
}

/**
 * iris_message_set_object:
 * @message: An #IrisMessage
 * @name: the key
 * @object: the value
 *
 * Updates @message to use @object as the value for @key.
 */
void
iris_message_set_object (IrisMessage *message,
                         const gchar *name,
                         GObject     *object)
{
	GValue *real_value;

	g_return_if_fail (message != NULL);

	real_value = iris_message_value_new (NULL);
	g_value_init (real_value, G_TYPE_OBJECT);
	g_value_set_object (real_value, object);

	iris_message_set_value_internal (message, name, real_value);
}
