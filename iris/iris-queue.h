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

G_BEGIN_DECLS

#define IRIS_TYPE_QUEUE            (iris_queue_get_type ())
#define IRIS_QUEUE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_QUEUE, IrisQueue))
#define IRIS_QUEUE_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_QUEUE, IrisQueue const))
#define IRIS_QUEUE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_QUEUE, IrisQueueClass))
#define IRIS_IS_QUEUE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_QUEUE))
#define IRIS_IS_QUEUE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_QUEUE))
#define IRIS_QUEUE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_QUEUE, IrisQueueClass))

typedef struct _IrisQueue        IrisQueue;
typedef struct _IrisQueueClass   IrisQueueClass;
typedef struct _IrisQueuePrivate IrisQueuePrivate;

struct _IrisQueue
{
	GObject parent;

	/*< private >*/
	IrisQueuePrivate *priv;
};

struct _IrisQueueClass
{
	GObjectClass parent_class;

	gboolean (*push)               (IrisQueue *queue,
	                                gpointer   data);
	gpointer (*pop)                (IrisQueue *queue);
	gpointer (*try_pop)            (IrisQueue *queue);
	gpointer (*timed_pop)          (IrisQueue *queue,
	                                GTimeVal  *timeout);
	gpointer (*try_pop_or_close)   (IrisQueue *queue);
	gpointer (*timed_pop_or_close) (IrisQueue *queue,
	                                GTimeVal  *timeout);
	void     (*close)              (IrisQueue *queue);

	guint    (*get_length)         (IrisQueue *queue);
	gboolean (*is_closed)          (IrisQueue *queue);
};

GType       iris_queue_get_type           (void) G_GNUC_CONST;
IrisQueue * iris_queue_new                (void);

gboolean    iris_queue_push               (IrisQueue *queue,
                                           gpointer   data);
gpointer    iris_queue_pop                (IrisQueue *queue);
gpointer    iris_queue_try_pop            (IrisQueue *queue);
gpointer    iris_queue_timed_pop          (IrisQueue *queue,
                                           GTimeVal  *timeout);
gpointer    iris_queue_try_pop_or_close   (IrisQueue *queue);
gpointer    iris_queue_timed_pop_or_close (IrisQueue *queue,
                                           GTimeVal  *timeout);
void        iris_queue_close              (IrisQueue *queue);

guint       iris_queue_get_length         (IrisQueue *queue);
gboolean    iris_queue_is_closed          (IrisQueue *queue);

G_END_DECLS

#endif /* __IRIS_QUEUE_H__ */
