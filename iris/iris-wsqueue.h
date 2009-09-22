/* iris-wsqueue.h
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

#ifndef __IRIS_WSQUEUE_H__
#define __IRIS_WSQUEUE_H__

#include <glib-object.h>

#include "iris-link.h"
#include "iris-queue.h"
#include "iris-rrobin.h"

G_BEGIN_DECLS

#define IRIS_TYPE_WSQUEUE (iris_wsqueue_get_type())
#define IRIS_WSQUEUE(q) ((IrisWSQueue*)q)

typedef struct _IrisWSQueue IrisWSQueue;

struct _IrisWSQueue
{
	IrisQueue      parent;

	/*< private >*/
	IrisQueue     *global;
	IrisRRobin    *rrobin;
	gint           mask;
	GMutex        *mutex;
	volatile gint  head_idx;
	volatile gint  tail_idx;
	gpointer      *items;
	gulong         length;
};

GType      iris_wsqueue_get_type   (void) G_GNUC_CONST;
IrisQueue* iris_wsqueue_new        (IrisQueue *global, IrisRRobin *peers);
gpointer   iris_wsqueue_try_steal  (IrisWSQueue *queue, guint timeout);
void       iris_wsqueue_local_push (IrisWSQueue *queue, gpointer data);
gpointer   iris_wsqueue_local_pop  (IrisWSQueue *queue);

G_END_DECLS

#endif /* __IRIS_WSQUEUE_H__ */
