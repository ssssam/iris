/* iris-thread.h
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

#ifndef __IRIS_THREAD_H__
#define __IRIS_THREAD_H__

#include <glib-object.h>

#include "iris-types.h"
#include "iris-scheduler.h"

G_BEGIN_DECLS

struct _IrisThread
{
	gpointer       user_data;
	gpointer       user_data2;
	gpointer       user_data3;

	/*< private >*/
	IrisScheduler *scheduler;  /* Pointer to scheduler       */
	GThread       *thread;     /* Handle to the thread       */
	GAsyncQueue   *queue;      /* Command queue              */
	gboolean       exclusive;  /* Can the thread be removed  *
	                            * from an active scheduler   */
	GMutex        *mutex;      /* Mutex for changing thread  *
	                            * state. e.g. active queue.  */
	GAsyncQueue   *active;     /* Active processing queue    */
};

struct _IrisThreadWork
{
	IrisCallback callback;
	gpointer     data;
};

/* Thread abstraction for schedulers */
IrisThread*     iris_thread_new        (gboolean exclusive);
IrisThread*     iris_thread_get        (void);
void            iris_thread_manage     (IrisThread *thread, GAsyncQueue *queue, gboolean leader);
void            iris_thread_shutdown   (IrisThread *thread);
void            iris_thread_print_stat (IrisThread *thread);

/* Contract for work items performed by IrisThread's */
IrisThreadWork* iris_thread_work_new   (IrisCallback callback, gpointer data);
void            iris_thread_work_free  (IrisThreadWork *thread_work);
void            iris_thread_work_run   (IrisThreadWork *thread_work);

G_END_DECLS

#endif /* __IRIS_THREAD_H__ */
