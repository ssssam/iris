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

#include <glib.h>

G_BEGIN_DECLS

typedef void (*IrisRRobinFunc) (gpointer data, gpointer user_data);

typedef struct _IrisRRobin IrisRRobin;

struct _IrisRRobin
{
	/*< private >*/
	gint     size;
	gint     count;
	guint    active;
	gpointer data[1];
};

IrisRRobin* iris_rrobin_new    (gint size);
gboolean    iris_rrobin_append (IrisRRobin *rrobin, gpointer data);
void        iris_rrobin_remove (IrisRRobin *rrobin, gpointer data);
gboolean    iris_rrobin_apply  (IrisRRobin *rrobin, IrisRRobinFunc callback, gpointer user_data);
void        iris_rrobin_free   (IrisRRobin *rrobin);

G_END_DECLS

#endif /* __IRIS_RROBIN_H__ */
