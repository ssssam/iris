/* iris-message.h
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

#ifndef __IRIS_MESSAGE_H__
#define __IRIS_MESSAGE_H__

#include <glib-object.h>

#include "iris-types.h"

G_BEGIN_DECLS

#define IRIS_TYPE_MESSAGE (iris_message_get_type())

struct _IrisMessage
{
	gint            what;

	/*< private >*/
	volatile gint   ref_count;
	GHashTable     *items;
};

GType        iris_message_get_type       (void);

IrisMessage* iris_message_new            (gint what);
IrisMessage* iris_message_new_full       (gint what, const gchar *first_name, ...);
 
IrisMessage* iris_message_ref            (IrisMessage *message);
void         iris_message_unref          (IrisMessage *message);
IrisMessage* iris_message_copy           (IrisMessage *message);

guint        iris_message_count_names    (IrisMessage *message);
gboolean     iris_message_is_empty       (IrisMessage *message);
gboolean     iris_message_contains       (IrisMessage *message, const gchar *name);

void         iris_message_get_value      (IrisMessage *message, const gchar *name, GValue *value);
void         iris_message_set_value      (IrisMessage *message, const gchar *name, const GValue *value);

const gchar* iris_message_get_string     (IrisMessage *message, const gchar *name);
void         iris_message_set_string     (IrisMessage *message, const gchar *name, const gchar *value);

gint         iris_message_get_int        (IrisMessage *message, const gchar *name);
void         iris_message_set_int        (IrisMessage *message, const gchar *name, gint value);

gint64       iris_message_get_int64      (IrisMessage *message, const gchar *name);
void         iris_message_set_int64      (IrisMessage *message, const gchar *name, gint64 value);

gfloat       iris_message_get_float      (IrisMessage *message, const gchar *name);
void         iris_message_set_float      (IrisMessage *message, const gchar *name, gfloat value);

gdouble      iris_message_get_double     (IrisMessage *message, const gchar *name);
void         iris_message_set_double     (IrisMessage *message, const gchar *name, gdouble value);

glong        iris_message_get_long       (IrisMessage *message, const gchar *name);
void         iris_message_set_long       (IrisMessage *message, const gchar *name, glong value);

gulong       iris_message_get_ulong      (IrisMessage *message, const gchar *name);
void         iris_message_set_ulong      (IrisMessage *message, const gchar *name, gulong value);

gchar        iris_message_get_char       (IrisMessage *message, const gchar *name);
void         iris_message_set_char       (IrisMessage *message, const gchar *name, gchar value);

guchar       iris_message_get_uchar      (IrisMessage *message, const gchar *name);
void         iris_message_set_uchar      (IrisMessage *message, const gchar *name, guchar value);

gboolean     iris_message_get_boolean    (IrisMessage *message, const gchar *name);
void         iris_message_set_boolean    (IrisMessage *message, const gchar *name, gboolean value);

gpointer     iris_message_get_pointer    (IrisMessage *message, const gchar *name);
void         iris_message_set_pointer    (IrisMessage *message, const gchar *name, gpointer value);

gsize        iris_message_flattened_size (IrisMessage *message);
gboolean     iris_message_flatten        (IrisMessage *message, gchar **buffer, gsize *length);
gboolean     iris_message_unflatten      (IrisMessage *message, gchar  *buffer, gsize  length);

G_END_DECLS

#endif /* __IRIS_MESSAGE_H__ */
