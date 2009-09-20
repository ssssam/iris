/* iris-queue.h
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

#ifndef __IRIS_QUEUE_H__
#define __IRIS_QUEUE_H__

#include <glib-object.h>

#include "iris-free-list.h"
#include "iris-link.h"

G_BEGIN_DECLS

#define IRIS_TYPE_QUEUE (iris_queue_get_type())

typedef struct _IrisQueue IrisQueue;

struct _IrisQueue
{
	/*< private >*/
	gpointer         vtable;
	volatile gint    ref_count;
	GAsyncQueue     *impl_queue;
};

GType      iris_queue_get_type  (void) G_GNUC_CONST;
IrisQueue* iris_queue_new       (void);

IrisQueue* iris_queue_ref       (IrisQueue *queue);
void       iris_queue_unref     (IrisQueue *queue);

void       iris_queue_push      (IrisQueue *queue, gpointer data);
gpointer   iris_queue_pop       (IrisQueue *queue);
gpointer   iris_queue_try_pop   (IrisQueue *queue);
gpointer   iris_queue_timed_pop (IrisQueue *queue, GTimeVal *timeout);
guint      iris_queue_length    (IrisQueue *queue);

G_END_DECLS

#endif /* __IRIS_QUEUE_H__ */
