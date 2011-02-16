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

#include <glib.h>

G_BEGIN_DECLS

struct _IrisQueuePrivate
{
	GAsyncQueue *q;

	/* 'open' should be accessed using g_atomic_int_* methods, because although
	 * it is only set inside the GAsyncQueue mutex it is also read in
	 * iris_queue_is_open().
	 */
	volatile gint open;

	/* Number of close tokens posted by iris_queue_close() */
	gint close_token_count;
};

G_END_DECLS

#endif /* __IRIS_QUEUE_PRIVATE_H__ */
