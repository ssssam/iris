/* iris-lfqueue.h
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

#ifndef __IRIS_LFQUEUE_H__
#define __IRIS_LFQUEUE_H__

#include <glib-object.h>

#include "iris-free-list.h"
#include "iris-link.h"
#include "iris-queue.h"
#include "iris-types.h"

G_BEGIN_DECLS

#define IRIS_TYPE_LFQUEUE (iris_lfqueue_get_type())

struct _IrisLFQueue
{
	IrisQueue     parent;

	/*< private >*/
	IrisLink     *head;
	IrisLink     *tail;
	IrisFreeList *free_list;
	guint         length;
};

GType      iris_lfqueue_get_type (void) G_GNUC_CONST;
IrisQueue* iris_lfqueue_new      (void);

G_END_DECLS

#endif /* __IRIS_LFQUEUE_H__ */
