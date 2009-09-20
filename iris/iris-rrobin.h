/* iris-rrobin.h
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

#ifndef __IRIS_RROBIN_H__
#define __IRIS_RROBIN_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define IRIS_TYPE_RROBIN (iris_rrobin_get_type())

typedef struct _IrisRRobin IrisRRobin;
typedef void     (*IrisRRobinFunc)        (gpointer data, gpointer user_data);
typedef gboolean (*IrisRRobinForeachFunc) (IrisRRobin *rrobin, gpointer data, gpointer user_data);

struct _IrisRRobin
{
	/*< private >*/
	gint          size;
	volatile gint ref_count;
	gint          count;
	guint         active;
	gpointer      data[1];
};

GType       iris_rrobin_get_type (void) G_GNUC_CONST;
IrisRRobin* iris_rrobin_new      (gint size);
IrisRRobin* iris_rrobin_ref      (IrisRRobin *rrobin);
void        iris_rrobin_unref    (IrisRRobin *rrobin);

gboolean    iris_rrobin_append   (IrisRRobin *rrobin, gpointer data);
void        iris_rrobin_remove   (IrisRRobin *rrobin, gpointer data);
gboolean    iris_rrobin_apply    (IrisRRobin *rrobin, IrisRRobinFunc callback, gpointer user_data);
void        iris_rrobin_foreach  (IrisRRobin *rrobin, IrisRRobinForeachFunc callback, gpointer user_data);

G_END_DECLS

#endif /* __IRIS_RROBIN_H__ */
