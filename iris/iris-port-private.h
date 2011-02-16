/* iris-port-private.h
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

#ifndef __IRIS_PORT_PRIVATE_H__
#define __IRIS_PORT_PRIVATE_H__

#include <glib-object.h>

#include "iris-message.h"
#include "iris-receiver.h"

struct _IrisPortPrivate
{
	IrisMessage *current;    /* Contains the current message in the
	                          * port. This happens if the receiver
	                          * did not accept our message and we
	                          * removed the receiver, or if we have
	                          * no receiver. After a message is in
	                          * current, all future post's go into
	                          * the queue.
	                          */

	GQueue *queue;           /* Queue for incoming messages that
	                          * cannot yet be delivered.
	                          */

	IrisReceiver *receiver;  /* Our receiver to deliver messages. */

	GMutex *mutex;           /* Mutex for synchronizing port access.
	                          * We try to avoid acquiring this lock
	                          * unless we cannot deliver to the
	                          * receiver.
	                          */

	/* Flags. Any may be accessed concurrently so always use g_atomics */
	/* FIXME: would be nice to merge these, and use some g_atomic bitfield operators ... */
	volatile gint paused;
	volatile gint flushing;  /* Must be paused while flushing */
};

#endif /* __IRIS_PORT_PRIVATE_H__ */
