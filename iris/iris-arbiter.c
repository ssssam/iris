/* iris-arbiter.c
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

#include "iris-arbiter.h"
#include "iris-arbiter-private.h"

struct _IrisArbiterPrivate
{
	gpointer dummy;
};

G_DEFINE_TYPE (IrisArbiter, iris_arbiter, G_TYPE_OBJECT);

static void
iris_arbiter_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_arbiter_parent_class)->finalize (object);
}

static void
iris_arbiter_class_init (IrisArbiterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = iris_arbiter_finalize;

	g_type_class_add_private (object_class, sizeof (IrisArbiterPrivate));
}

static void
iris_arbiter_init (IrisArbiter *arbiter)
{
	arbiter->priv = G_TYPE_INSTANCE_GET_PRIVATE (arbiter,
	                                          IRIS_TYPE_ARBITER,
	                                          IrisArbiterPrivate);
}

IrisArbiter*
iris_arbiter_new (void)
{
	return g_object_new (IRIS_TYPE_ARBITER, NULL);
}
