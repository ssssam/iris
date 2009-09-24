/* iris-queue-private.h
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

#ifndef __IRIS_QUEUE_PRIVATE_H__
#define __IRIS_QUEUE_PRIVATE_H__

G_BEGIN_DECLS

#define VTABLE(q) ((IrisQueueVTable*)(q->vtable))

typedef struct _IrisQueueVTable IrisQueueVTable;

struct _IrisQueueVTable
{
	void     (*push)      (IrisQueue *queue, gpointer data);
	gpointer (*pop)       (IrisQueue *queue);
	gpointer (*try_pop)   (IrisQueue *queue);
	gpointer (*timed_pop) (IrisQueue *queue, GTimeVal *timeout);
	guint    (*length)    (IrisQueue *queue);
	void     (*dispose)   (IrisQueue *queue);
};

G_END_DECLS

#endif /* __IRIS_QUEUE_PRIVATE_H__ */
