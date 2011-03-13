/* iris-task.h
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

#ifndef __IRIS_TASK_H__
#define __IRIS_TASK_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "iris-message.h"
#include "iris-progress.h"
#include "iris-scheduler.h"

G_BEGIN_DECLS

#define IRIS_TYPE_TASK            (iris_task_get_type ())
#define IRIS_TASK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_TASK, IrisTask))
#define IRIS_TASK_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_TASK, IrisTask const))
#define IRIS_TASK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_TASK, IrisTaskClass))
#define IRIS_IS_TASK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_TASK))
#define IRIS_IS_TASK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_TASK))
#define IRIS_TASK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_TASK, IrisTaskClass))

/**
 * IRIS_TASK_THROW_NEW:
 * @t: An #IrisTask
 * @q: A #GQuark
 * @c: a #gint containing the error code
 * @f: the error format string
 * @...: parameters for format
 *
 * Creates a new #GError and attaches it to the task.
 */
#define IRIS_TASK_THROW_NEW(t,q,c,f,...)                                    \
        G_STMT_START {                                                      \
                GError *_iris_task_error = g_error_new(q,c,f,##__VA_ARGS__);\
                iris_task_take_fatal_error (t, _iris_task_error);           \
        } G_STMT_END

/**
 * IRIS_TASK_THROW:
 * @t: An #IrisTask
 * @e: A #GError
 *
 * Steals ownership of @e and attaches the #GError to the task.
 */
#define IRIS_TASK_THROW(t,e)                                                \
        G_STMT_START {                                                      \
                iris_task_take_fatal_error(t, e);                            \
        } G_STMT_END

/**
 * IRIS_TASK_CATCH:
 * @t: An #IrisTask
 * @e: a location for a #GError or %NULL
 *
 * Catches the currently set error on the task and stores it into @e.
 * The ownership of @e is that of the caller so it should be freed
 * with g_error_free() if non-%NULL.
 */
#define IRIS_TASK_CATCH(t,e)                                                \
        G_STMT_START {                                                      \
                if (e != NULL)                                              \
                    iris_task_get_fatal_error (t,e);                        \
                iris_task_set_fatal_error (t, NULL);                        \
        } G_STMT_END

/**
 * IRIS_TASK_RETURN_VALUE:
 * @t: An #IrisTask
 * @gt: A #GType
 * @v: the value to store
 *
 * This macro simplifies how you can store the current result for the task.
 * It can transparently store the value of @v no matter the type as long as
 * your GType is accurate.
 *
 * For example, to store an int you could
 *
 * |[
 * gint myint = 0;
 * IRIS_TASK_RETURN_VALUE (t, G_TYPE_INT, myint);
 * ]|
 */
#define IRIS_TASK_RETURN_VALUE(t,gt,v)                                      \
        iris_task_set_result_gtype(t,gt,v)

/**
 * IRIS_TASK_RETURN_TASK:
 * @t: An #IrisTask
 * @t2: An #IrisTask
 *
 * Sets the result of a task to another task.  This will prevent the task from
 * any further work in the callback/errback phase until @t2 has completed.
 */
#define IRIS_TASK_RETURN_TASK(t,t2)                                         \
        G_STMT_START {                                                      \
                iris_task_add_dependency(t,t2);                             \
        } G_STMT_END

/**
 * IRIS_TASK_RETURN_TASK_NEW:
 * @t: An #IrisTask
 * @f: An #IrisTaskFunc
 * @p: user data for the newly created #IrisTask
 * @n: A #GDestroyNotify to be called when the new task is destroyed
 *
 * Helper macro to create and return a new task as the result in one step.
 */
#define IRIS_TASK_RETURN_TASK_NEW(t,f,p,n)                                  \
        G_STMT_START {                                                      \
                IrisTask *t2 = iris_task_new(f,p,n);              \
                iris_task_add_dependency(t,t2);                             \
                g_object_unref(t2);                                         \
        } G_STMT_END

typedef struct _IrisTask        IrisTask;
typedef struct _IrisTaskClass   IrisTaskClass;
typedef struct _IrisTaskPrivate IrisTaskPrivate;

/**
 * IrisTaskFunc:
 * @task: An #IrisTask
 * @user_data: user specified data
 *
 * Callback for tasks, callbacks, and errbacks.
 * See iris_task_add_callback(), iris_task_add_errback().
 */
typedef void (*IrisTaskFunc) (IrisTask *task, gpointer user_data);

struct _IrisTask
{
	GObject parent;
	
	/*< private >*/
	IrisTaskPrivate *priv;
};

struct _IrisTaskClass
{
	GObjectClass parent_class;

	void     (*handle_message)       (IrisTask *task, IrisMessage *message);

	void     (*execute)              (IrisTask *task);
	gboolean (*can_cancel)           (IrisTask *task);

	gboolean (*has_succeeded)        (IrisTask *task);
	gboolean (*has_failed)           (IrisTask *task);

	void     (*dependency_cancelled) (IrisTask *task, IrisTask *dep);
	void     (*dependency_finished)  (IrisTask *task, IrisTask *dep);

	void     (*reserved1)            (void);
	void     (*reserved2)            (void);
	void     (*reserved3)            (void);
	void     (*reserved4)            (void);
};

GType         iris_task_get_type              (void) G_GNUC_CONST;
IrisTask*     iris_task_new                   (IrisTaskFunc         func,
                                               gpointer             user_data,
                                               GDestroyNotify       notify);
IrisTask*     iris_task_new_with_closure      (GClosure            *closure);
IrisTask*     iris_task_new_full              (IrisTaskFunc         func,
                                               gpointer             user_data,
                                               GDestroyNotify       notify,
                                               gboolean             async,
                                               IrisScheduler       *control_scheduler,
                                               IrisScheduler       *work_scheduler,
                                               GMainContext        *context);
IrisTask*     iris_task_new_with_closure_full (GClosure            *closure,
                                               gboolean             async,
                                               IrisScheduler       *control_scheduler,
                                               IrisScheduler       *work_scheduler,
                                               GMainContext        *context);

void          iris_task_run                   (IrisTask            *task);
void          iris_task_run_with_async_result (IrisTask            *task,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data);
void          iris_task_cancel                (IrisTask            *task);
void          iris_task_work_finished         (IrisTask            *task);

void          iris_task_set_progress_mode     (IrisTask            *task,
                                               IrisProgressMode     mode);

IrisProgressMode iris_task_get_progress_mode  (IrisTask            *task);

void          iris_task_add_callback          (IrisTask            *task,
                                               IrisTaskFunc         callback,
                                               gpointer             user_data,
                                               GDestroyNotify       notify);
void          iris_task_add_errback           (IrisTask            *task,
                                               IrisTaskFunc         errback,
                                               gpointer             user_data,
                                               GDestroyNotify       notify);
void          iris_task_add_callback_closure  (IrisTask            *task,
                                               GClosure            *closure);
void          iris_task_add_errback_closure   (IrisTask            *task,
                                               GClosure            *closure);
void          iris_task_add_both              (IrisTask            *task,
                                               IrisTaskFunc         callback,
                                               IrisTaskFunc         errback,
                                               gpointer             user_data,
                                               GDestroyNotify       notify);
void          iris_task_add_both_closure      (IrisTask            *task,
                                               GClosure            *callback,
                                               GClosure            *errback);

void          iris_task_add_dependency        (IrisTask            *task,
                                               IrisTask            *dependency);

gboolean      iris_task_is_async              (IrisTask            *task);

gboolean      iris_task_is_executing          (IrisTask            *task);
gboolean      iris_task_is_finished           (IrisTask            *task);
gboolean      iris_task_has_succeeded         (IrisTask            *task);
gboolean      iris_task_has_failed            (IrisTask            *task);
gboolean      iris_task_is_cancelled          (IrisTask            *task);

gboolean      iris_task_get_fatal_error       (IrisTask            *task,
                                               GError             **error);
void          iris_task_set_fatal_error       (IrisTask            *task,
                                               const GError        *error);
void          iris_task_take_fatal_error      (IrisTask            *task,
                                               GError              *error);

void          iris_task_get_result            (IrisTask            *task,
                                               GValue              *value);
void          iris_task_set_result            (IrisTask            *task,
                                               const GValue        *value);
void          iris_task_set_result_gtype      (IrisTask            *task,
                                               GType                type, ...);

void          iris_task_set_main_context      (IrisTask            *task,
                                               GMainContext        *context);
GMainContext* iris_task_get_main_context      (IrisTask            *task);

IrisTask*     iris_task_vall_of               (IrisTask            *first_task, ...) __attribute__ ((__sentinel__));
IrisTask*     iris_task_all_of                (GList *tasks);

IrisTask*     iris_task_vany_of               (IrisTask            *first_task, ...) __attribute__ ((__sentinel__));
IrisTask*     iris_task_any_of                (GList *tasks);

G_END_DECLS

#endif /* __IRIS_TASK_H__ */
