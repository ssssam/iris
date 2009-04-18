/* iris-coordination-arbiter-private.h
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

#ifndef __IRIS_COORDINATION_ARBITER_PRIVATE_H__
#define __IRIS_COORDINATION_ARBITER_PRIVATE_H__

#include <glib-object.h>

#include "iris-arbiter.h"
#include "iris-receiver.h"

typedef enum
{
	IRIS_COORD_EXCLUSIVE        = 1 << 0,
	IRIS_COORD_NEEDS_EXCLUSIVE  = 1 << 1,
	IRIS_COORD_CONCURRENT       = 1 << 2,
	IRIS_COORD_NEEDS_CONCURRENT = 1 << 3,
	IRIS_COORD_NEEDS_TEARDOWN   = 1 << 4,
	IRIS_COORD_TEARDOWN         = 1 << 5,
} IrisCoordinationFlags;

struct _IrisCoordinationArbiterPrivate
{
	IrisReceiver *exclusive;
	IrisReceiver *concurrent;
	IrisReceiver *teardown;
	GMutex       *mutex;
	guint         flags;
	glong         active;
};

#endif /* __IRIS_COORDINATION_ARBITER_PRIVATE_H__ */