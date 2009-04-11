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

#include <glib.h>

#include "iris-types.h"
#include "iris-free-list.h"
#include "iris-link.h"

G_BEGIN_DECLS

struct _IrisQueueVTable
{
	void     (*enqueue)    (IrisQueue *queue, gpointer data);
	gpointer (*dequeue)    (IrisQueue *queue);
	guint    (*get_length) (IrisQueue *queue);
	void     (*destroy)    (IrisQueue *queue);
};

struct _IrisQueue
{
	IrisQueueVTable *vtable;
	volatile gint    ref_count;
	IrisLink        *head;
	IrisLink        *tail;
	IrisFreeList    *free_list;
	guint            length;
};

IrisQueue* iris_queue_new        (void);
IrisQueue* iris_queue_ref        (IrisQueue *queue);
void       iris_queue_unref      (IrisQueue *queue);
void       iris_queue_enqueue    (IrisQueue *queue, gpointer data);
gpointer   iris_queue_dequeue    (IrisQueue *queue);
guint      iris_queue_get_length (IrisQueue *queue);

G_END_DECLS

#endif /* __IRIS_QUEUE_H__ */
