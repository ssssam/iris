/* iris-scheduler.h
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

#ifndef __IRIS_SCHEDULER_H__
#define __IRIS_SCHEDULER_H__

#include <glib-object.h>

#include "iris-queue.h"

G_BEGIN_DECLS

#define IRIS_TYPE_SCHEDULER (iris_scheduler_get_type ())
#define IRIS_TYPE_THREAD    (iris_thread_get_type())

#define IRIS_SCHEDULER(obj)                  \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),      \
     IRIS_TYPE_SCHEDULER, IrisScheduler))

#define IRIS_SCHEDULER_CONST(obj)            \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),      \
     IRIS_TYPE_SCHEDULER, IrisScheduler const))

#define IRIS_SCHEDULER_CLASS(klass)          \
    (G_TYPE_CHECK_CLASS_CAST ((klass),       \
     IRIS_TYPE_SCHEDULER, IrisSchedulerClass))

#define IRIS_IS_SCHEDULER(obj)               \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj),      \
     IRIS_TYPE_SCHEDULER))

#define IRIS_IS_SCHEDULER_CLASS(klass)       \
    (G_TYPE_CHECK_CLASS_TYPE ((klass),       \
     IRIS_TYPE_SCHEDULER))

#define IRIS_SCHEDULER_GET_CLASS(obj)        \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),       \
     IRIS_TYPE_SCHEDULER, IrisSchedulerClass))

typedef struct _IrisScheduler        IrisScheduler;
typedef struct _IrisSchedulerClass   IrisSchedulerClass;
typedef struct _IrisSchedulerPrivate IrisSchedulerPrivate;
typedef struct _IrisThread           IrisThread;
typedef struct _IrisThreadWork       IrisThreadWork;
typedef void   (*IrisCallback)       (gpointer data);

/**
 * IrisSchedulerForeachFunc:
 * @scheduler: the #IrisScheduler executing the foreach
 * @work_item: internal pointer to the work, suitable for passing to
 *             iris_scheduler_unqueue()
 * @callback: callback for the work item
 * @data: data for the work item
 * @user_data: user data passed to iris_scheduler_foreach()
 * 
 * Callback for each work item during iris_scheduler_foreach(). Remember that
 * this function will be called for every piece of work in the scheduler's
 * queue, and it is up to the caller to filter for only their items using
 * the information in @callback and @data.
 * 
 * Returns: %FALSE to abort the foreach, %TRUE to continue.
 */
typedef gboolean (*IrisSchedulerForeachFunc) (IrisScheduler *scheduler,
                                              gpointer work_item,
                                              IrisCallback callback,
                                              gpointer data,
                                              gpointer user_data);

struct _IrisScheduler
{
	GObject parent;

	/*< private >*/
	IrisSchedulerPrivate *priv;
	volatile gint        in_finalize;
	volatile gint        maxed;
};

struct _IrisSchedulerClass
{
	GObjectClass  parent_class;

	gint  (*get_max_threads) (IrisScheduler  *scheduler);
	gint  (*get_min_threads) (IrisScheduler  *scheduler);

	void     (*queue)        (IrisScheduler  *scheduler,
	                          IrisCallback    func,
	                          gpointer        data,
	                          GDestroyNotify  destroy_notify);
	gboolean (*unqueue)      (IrisScheduler  *scheduler,
	                          gpointer        work_item);
	void     (*foreach)      (IrisScheduler            *scheduler,
	                          IrisSchedulerForeachFunc  callback,
	                          gpointer                  user_data);

	void    (*add_thread)    (IrisScheduler  *scheduler,
	                          IrisThread     *thread,
	                          gboolean        exclusive);
	void    (*remove_thread) (IrisScheduler  *scheduler,
	                          IrisThread     *thread);
};

struct _IrisThread
{
	gpointer       user_data;
	gpointer       user_data2;
	gpointer       user_data3;

	/*< private >*/

	/* 'scheduler' is set by the scheduler manager when a thread is added, and
	 * is unset by the thread when the thread is removed. No memory guards are
	 * needed except during removal, where the scheduler waits for this pointer
	 * to == NULL so it knows the thread is idle.
	 */
	IrisScheduler * volatile scheduler;

	GThread                 *thread;     /* Handle to the thread       */
	GAsyncQueue             *queue;      /* Command queue              */
	gboolean                 exclusive;  /* Can the thread be removed  *
	                                      * from an active scheduler   */
	GMutex                  *mutex;      /* Mutex for changing thread  *
	                                      * state. e.g. active queue.  */
	IrisQueue               *active;     /* Active processing queue, or NULL if idle */
};

struct _IrisThreadWork
{
	IrisCallback      callback;
	gpointer          data;
	GDestroyNotify    notify;

	/* FIXME: would be nice to make these flags, but need to stay atomic */
	volatile gint     taken;
	volatile gint     remove;
};

GType           iris_thread_get_type           (void) G_GNUC_CONST;
GType           iris_scheduler_get_type        (void) G_GNUC_CONST;

IrisScheduler*  iris_scheduler_new             (void);
IrisScheduler*  iris_scheduler_new_full        (guint           min_threads,
                                               guint           max_threads);
IrisScheduler*  iris_scheduler_default         (void);
void            iris_scheduler_set_default     (IrisScheduler *scheduler);

gint            iris_scheduler_get_min_threads (IrisScheduler  *scheduler);
gint            iris_scheduler_get_max_threads (IrisScheduler  *scheduler);

void            iris_scheduler_queue           (IrisScheduler  *scheduler,
                                                IrisCallback    func,
                                                gpointer        data,
                                                GDestroyNotify  destroy_notify);
gboolean        iris_scheduler_unqueue         (IrisScheduler  *scheduler,
                                                gpointer        work_item);
void            iris_scheduler_foreach         (IrisScheduler            *scheduler,
                                                IrisSchedulerForeachFunc  callback,
                                                gpointer                  user_data);

void            iris_scheduler_add_thread      (IrisScheduler  *scheduler,
                                                IrisThread     *thread,
                                                gboolean        exclusive);
void            iris_scheduler_remove_thread   (IrisScheduler  *scheduler,
                                                IrisThread     *thread);

IrisThread*     iris_thread_new                (gboolean exclusive);
IrisThread*     iris_thread_get                (void);
gboolean        iris_thread_is_working         (IrisThread *thread);
void            iris_thread_manage             (IrisThread *thread,
                                                IrisQueue  *queue,
                                                gboolean    exclusive,
                                                gboolean    leader);
void            iris_thread_shutdown           (IrisThread *thread);
void            iris_thread_print_stat         (IrisThread *thread);
IrisThreadWork* iris_thread_work_new           (IrisCallback    callback,
                                                gpointer        data,
                                                GDestroyNotify  destroy_notify);
void            iris_thread_work_free          (IrisThreadWork *thread_work);
void            iris_thread_work_run           (IrisThreadWork *thread_work);

guint           iris_scheduler_get_n_cpu       ();

G_END_DECLS

#endif /* __IRIS_SCHEDULER_H__ */
