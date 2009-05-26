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

G_BEGIN_DECLS

#define IRIS_TYPE_TASK (iris_task_get_type ())

#define IRIS_TASK(obj)                      \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
         IRIS_TYPE_TASK, IrisTask))

#define IRIS_TASK_CONST(obj)                \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
         IRIS_TYPE_TASK, IrisTask const))

#define IRIS_TASK_CLASS(klass)              \
        (G_TYPE_CHECK_CLASS_CAST ((klass),  \
         IRIS_TYPE_TASK, IrisTaskClass))

#define IRIS_IS_TASK(obj)                   \
        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
         IRIS_TYPE_TASK))

#define IRIS_IS_TASK_CLASS(klass)           \
        (G_TYPE_CHECK_CLASS_TYPE ((klass),  \
         IRIS_TYPE_TASK))

#define IRIS_TASK_GET_CLASS(obj)           \
        (G_TYPE_INSTANCE_GET_CLASS ((obj), \
         IRIS_TYPE_TASK, IrisTaskClass))

#define IRIS_TASK_THROW_NEW(t,q,c,f,...)                                    \
        G_STMT_START {                                                      \
                GError *_iris_task_error = g_error_new(q,c,f,##__VA_ARGS__);\
                iris_task_take_error (t,_iris_task_error);                  \
        } G_STMT_END

#define IRIS_TASK_THROW(t,e)                                                \
        G_STMT_START {                                                      \
                iris_task_take_error(t,e);                                  \
        } G_STMT_END

#define IRIS_TASK_CATCH(t,e)                                                \
        G_STMT_START {                                                      \
                if (e && *((GError**)e) == NULL && iris_task_get_error(t)) {\
                        *((GError**)e) = g_error_copy (                     \
                        		iris_task_get_error (t));           \
                }                                                           \
                iris_task_set_error (t,NULL);                               \
        } G_STMT_END

#define IRIS_TASK_RETURN_VALUE(t,gt,v)                                      \
        iris_task_set_result_gtype(t,gt,v)

#define IRIS_TASK_RETURN_TASK(t,t2)                                         \
        G_STMT_START {                                                      \
                iris_task_add_dependency(t,t2);                             \
        } G_STMT_END

#define IRIS_TASK_RETURN_TASK_NEW(t,f,p,n)                                  \
        G_STMT_START {                                                      \
                IrisTask *t2 = iris_task_new(f,p,n);                        \
                iris_task_add_dependency(t,tt2);                            \
                g_object_unref(t2);                                         \
        } G_STMT_END

typedef struct _IrisTask        IrisTask;
typedef struct _IrisTaskClass   IrisTaskClass;
typedef struct _IrisTaskPrivate IrisTaskPrivate;

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

	void     (*handle_message)      (IrisTask *task, IrisMessage *message);

	void     (*execute)             (IrisTask *task);
	gboolean (*cancel)              (IrisTask *task);

	void     (*dependency_canceled) (IrisTask *task, IrisTask *dep);
	void     (*dependency_finished) (IrisTask *task, IrisTask *dep);

	void     (*reserved1)           (void);
	void     (*reserved2)           (void);
	void     (*reserved3)           (void);
	void     (*reserved4)           (void);
};

GType         iris_task_get_type             (void) G_GNUC_CONST;
IrisTask*     iris_task_new                  (void);
IrisTask*     iris_task_new_with_func        (IrisTaskFunc         func,
                                              gpointer             user_data,
                                              GDestroyNotify       notify);
IrisTask*     iris_task_new_with_closure     (GClosure            *closure);
IrisTask*     iris_task_new_full             (IrisTaskFunc         func,
                                              gpointer             user_data,
                                              GDestroyNotify       notify,
                                              gboolean             async,
                                              IrisScheduler       *scheduler,
                                              GMainContext        *context);

void          iris_task_run                  (IrisTask            *task);
void          iris_task_run_full             (IrisTask            *task,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data);
void          iris_task_cancel               (IrisTask            *task);
void          iris_task_complete             (IrisTask            *task);

void          iris_task_add_callback         (IrisTask            *task,
                                              IrisTaskFunc         callback,
                                              gpointer             user_data,
                                              GDestroyNotify       notify);
void          iris_task_add_errback          (IrisTask            *task,
                                              IrisTaskFunc         errback,
                                              gpointer             user_data,
                                              GDestroyNotify       notify);
void          iris_task_add_callback_closure (IrisTask            *task,
                                              GClosure            *closure);
void          iris_task_add_errback_closure  (IrisTask            *task,
                                              GClosure            *closure);

void          iris_task_add_dependency       (IrisTask            *task,
                                              IrisTask            *dependency);
void          iris_task_remove_dependency    (IrisTask            *task,
                                              IrisTask            *dependency);

gboolean      iris_task_is_async             (IrisTask            *task);
gboolean      iris_task_is_executing         (IrisTask            *task);
gboolean      iris_task_is_canceled          (IrisTask            *task);
gboolean      iris_task_is_finished          (IrisTask            *task);

void          iris_task_set_main_context     (IrisTask            *task,
                                              GMainContext        *context);
GMainContext* iris_task_get_main_context     (IrisTask            *task);
void          iris_task_set_scheduler        (IrisTask            *task,
                                              IrisScheduler       *scheduler);

G_CONST_RETURN
GError*       iris_task_get_error            (IrisTask            *task);
void          iris_task_set_error            (IrisTask            *task,
                                              const GError        *error);
void          iris_task_take_error           (IrisTask            *task,
                                              GError              *error);

G_CONST_RETURN
GValue*       iris_task_get_result           (IrisTask            *task);
void          iris_task_set_result           (IrisTask            *task,
                                              const GValue        *value);
void          iris_task_set_result_gtype     (IrisTask            *task,
                                              GType                type, ...);

IrisTask*     iris_task_vall_of               (IrisTask            *first_task, ...) __attribute__ ((__sentinel__));
IrisTask*     iris_task_all_of                (GList *tasks);

IrisTask*     iris_task_any_of               (IrisTask            *first_task, ...) __attribute__ ((__sentinel__));
IrisTask*     iris_task_n_of                 (gint                 n,
                                              IrisTask            *first_task, ...) __attribute__ ((__sentinel__));

G_END_DECLS

#endif /* __IRIS_TASK_H__ */
