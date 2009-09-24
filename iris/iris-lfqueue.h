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

#include "iris-queue.h"

G_BEGIN_DECLS

#define IRIS_TYPE_LFQUEUE            (iris_lfqueue_get_type ())
#define IRIS_LFQUEUE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_LFQUEUE, IrisLFQueue))
#define IRIS_LFQUEUE_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_LFQUEUE, IrisLFQueue const))
#define IRIS_LFQUEUE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_LFQUEUE, IrisLFQueueClass))
#define IRIS_IS_LFQUEUE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_LFQUEUE))
#define IRIS_IS_LFQUEUE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_LFQUEUE))
#define IRIS_LFQUEUE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_LFQUEUE, IrisLFQueueClass))

typedef struct _IrisLFQueue        IrisLFQueue;
typedef struct _IrisLFQueueClass   IrisLFQueueClass;
typedef struct _IrisLFQueuePrivate IrisLFQueuePrivate;

struct _IrisLFQueue
{
	IrisQueue parent;

	/*< private >*/
	IrisLFQueuePrivate *priv;
};

struct _IrisLFQueueClass
{
	IrisQueueClass parent_class;
};

GType      iris_lfqueue_get_type (void) G_GNUC_CONST;
IrisQueue* iris_lfqueue_new      (void);

G_END_DECLS

#endif /* __IRIS_LFQUEUE_H__ */
