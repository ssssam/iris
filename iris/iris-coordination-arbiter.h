/* iris-coordination-arbiter.h
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

#ifndef __IRIS_COORDINATION_ARBITER_H__
#define __IRIS_COORDINATION_ARBITER_H__

#include <glib-object.h>

#include "iris-types.h"
#include "iris-arbiter.h"

G_BEGIN_DECLS

#define IRIS_TYPE_COORDINATION_ARBITER (iris_coordination_arbiter_get_type ())

#define IRIS_COORDINATION_ARBITER(obj)             \
	(G_TYPE_CHECK_INSTANCE_CAST ((obj),        \
	 IRIS_TYPE_COORDINATION_ARBITER,           \
	 IrisCoordinationArbiter))

#define IRIS_COORDINATION_ARBITER_CONST(obj)       \
	(G_TYPE_CHECK_INSTANCE_CAST ((obj),        \
	 IRIS_TYPE_COORDINATION_ARBITER,           \
	 IrisCoordinationArbiter const))

#define IRIS_COORDINATION_ARBITER_CLASS(klass)     \
	(G_TYPE_CHECK_CLASS_CAST ((klass),         \
	 IRIS_TYPE_COORDINATION_ARBITER,           \
	 IrisCoordinationArbiterClass))

#define IRIS_IS_COORDINATION_ARBITER(obj)          \
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj),        \
	 IRIS_TYPE_COORDINATION_ARBITER))

#define IRIS_IS_COORDINATION_ARBITER_CLASS(klass)  \
	(G_TYPE_CHECK_CLASS_TYPE ((klass),         \
	 IRIS_TYPE_COORDINATION_ARBITER))

#define IRIS_COORDINATION_ARBITER_GET_CLASS(obj)   \
	(G_TYPE_INSTANCE_GET_CLASS ((obj),         \
	 IRIS_TYPE_COORDINATION_ARBITER,           \
	 IrisCoordinationArbiterClass))

typedef struct _IrisCoordinationArbiter        IrisCoordinationArbiter;
typedef struct _IrisCoordinationArbiterClass   IrisCoordinationArbiterClass;
typedef struct _IrisCoordinationArbiterPrivate IrisCoordinationArbiterPrivate;

struct _IrisCoordinationArbiter
{
	IrisArbiter parent;
	
	IrisCoordinationArbiterPrivate *priv;
};

struct _IrisCoordinationArbiterClass
{
	IrisArbiterClass parent_class;
};

GType iris_coordination_arbiter_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __IRIS_COORDINATION_ARBITER_H__ */
