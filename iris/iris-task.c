/* iris-task.c
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

#include <string.h>
#include <gobject/gvaluecollector.h>

#include "iris-gmainscheduler.h"
#include "iris-receiver-private.h"
#include "iris-task.h"
#include "iris-task-private.h"

#define TOGGLE_CALLBACKS(t)                                             \
	g_atomic_int_set (&t->priv->flags,                              \
		((t->priv->flags & ~IRIS_TASK_FLAG_EXECUTING)           \
		 | IRIS_TASK_FLAG_CALLBACKS))
#define TOGGLE_FINISHED(t)                                              \
	g_atomic_int_set (&t->priv->flags,                              \
		(((t->priv->flags & ~IRIS_TASK_FLAG_EXECUTING)          \
		  & ~IRIS_TASK_FLAG_CALLBACKS)                          \
		 | IRIS_TASK_FLAG_FINISHED))
#define CAN_EXECUTE_NOW(t)                                              \
	(((t->priv->flags & IRIS_TASK_FLAG_EXECUTING) == 0) &&          \
	 ((t->priv->flags & IRIS_TASK_FLAG_CALLBACKS) == 0) &&          \
	 (t->priv->dependencies == NULL))
#define CAN_CALLBACK_NOW(t)                                             \
	((((t->priv->dependencies == NULL)                   &&         \
	 (((t->priv->flags & IRIS_TASK_FLAG_EXECUTING) != 0) ||         \
	   (t->priv->flags & IRIS_TASK_FLAG_CALLBACKS) != 0))))
#define CAN_FINISH_NOW(t)                                               \
	((t->priv->dependencies == NULL) &&                             \
	 (t->priv->handlers == NULL) &&				        \
	 ((t->priv->flags & IRIS_TASK_FLAG_CALLBACKS) != 0))
#define RUN_NEXT_HANDLER(t)                                             \
	G_STMT_START {                                                  \
		IrisTaskHandler *h;                                     \
		if (t->priv->error)                                     \
			h = iris_task_next_errback(t);                  \
		else                                                    \
			h = iris_task_next_callback(t);                 \
		if (h) {                                                \
			GValue param = {0,};                            \
			g_value_init (&param, G_TYPE_OBJECT);           \
			g_value_set_object (&param, t);                 \
			if (t->priv->error)                             \
				g_closure_invoke(h->errback,            \
				                 NULL,1,&param,NULL);   \
			else                                            \
				g_closure_invoke(h->callback,           \
				                 NULL,1,&param,NULL);   \
			g_value_unset (&param);                         \
			iris_task_handler_free (h);                     \
		}                                                       \
	} G_STMT_END
#define FLAG_IS_ON(t,f) ((t->priv->flags & f) != 0)
#define FLAG_IS_OFF(t,f) ((t->priv->flags & f) == 0)
#define ENABLE_FLAG(t,f) G_STMT_START{t->priv->flags|=f;}G_STMT_END
#define DISABLE_FLAG(t,f) G_STMT_START{t->priv->flags&=~f;}G_STMT_END
#define PROGRESS_BLOCKED(t)                                             \
	(t->priv->dependencies != NULL &&                               \
	 FLAG_IS_OFF (t, IRIS_TASK_FLAG_CANCELED))

G_DEFINE_TYPE (IrisTask, iris_task, G_TYPE_OBJECT);

static void             iris_task_dummy         (IrisTask *task, gpointer user_data);
static void             iris_task_handler_free  (IrisTaskHandler *handler);
static IrisTaskHandler* iris_task_next_handler  (IrisTask *task);
static IrisTaskHandler* iris_task_next_callback (IrisTask *task);
static IrisTaskHandler* iris_task_next_errback  (IrisTask *task);

/* We keep a list of main schedulers for processing work
 * work items in a main thread.  We have one scheduler for
 * each main-context that is used.  Of course, this is only
 * done if a task requests a main-context.
 */
static GList* main_schedulers = NULL;

/**************************************************************************
 *                          IrisTask Public API                           *
 *************************************************************************/

/**
 * iris_task_new:
 *
 * Creates a new instance of #IrisTask.
 *
 * Return value: the newly created instance of #IrisTask
 */
IrisTask*
iris_task_new (void)
{
	return g_object_new (IRIS_TYPE_TASK, NULL);
}

/**
 * iris_task_new_with_func:
 * @func: An #IrisTaskFunc to execute
 * @user_data: user data for @func
 * @notify: An optional #GDestroyNotify or %NULL
 *
 * Create a new #IrisTask instance.
 *
 * Return value: the newly created #IrisTask instance
 */
IrisTask*
iris_task_new_with_func (IrisTaskFunc   func,
                         gpointer       user_data,
                         GDestroyNotify notify)
{
	IrisTask *task;
	GClosure *closure;

	if (!func)
		func = iris_task_dummy;

	closure = g_cclosure_new (G_CALLBACK (func),
	                          user_data,
	                          (GClosureNotify)notify);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	task = iris_task_new_with_closure (closure);
	g_closure_unref (closure);

	return task;
}

/**
 * iris_task_new_full:
 * @func: An #IrisTaskFunc
 * @user_data: data for @func
 * @notify: A destroy notify after execution of the task
 * @async: Will the task complete during the execution of @func
 * @scheduler: An #IrisScheduler or %NULL
 * @context: A #GMainContext or %NULL
 *
 * Creates a new instance of #IrisTask.  This method allows for setting
 * if the task is asynchronous with @async.  An asynchronous task has the
 * ability to not complete during the execution of the task's execution
 * method (in this case @func).  To mark the task's execution as completed,
 * g_task_complete() must be called for the task.
 *
 * If you want errbacks and callbacks to complete within a #GMainContext,
 * you may specify @context or %NULL for the callbacks to happen within
 * the worker thread.
 *
 * @scheduler allows you to set a specific #IrisScheduler to perform
 * execution of the task within.  Note that all message passing associated
 * with the tasks internal #IrisPort<!-- -->'s will also happen on this
 * scheduler.
 *
 * Return value: The newly created #IrisTask instance.
 */
IrisTask*
iris_task_new_full (IrisTaskFunc   func,
                    gpointer       user_data,
                    GDestroyNotify notify,
                    gboolean       async,
                    IrisScheduler *scheduler,
                    GMainContext  *context)
{
	IrisTask *task = iris_task_new_with_func (func, user_data, notify);
	if (scheduler)
		iris_task_set_scheduler (task, scheduler);
	if (async)
		task->priv->flags |= IRIS_TASK_FLAG_ASYNC;
	if (context)
		iris_task_set_main_context (task, context);
	return task;
}

/**
 * iris_task_new_with_closure:
 * @closure: A #GClosure
 *
 * Creates a new task using the closure for execution.
 *
 * Return value: the newly created #IrisTask
 */
IrisTask*
iris_task_new_with_closure (GClosure *closure)
{
	IrisTask *task;

	g_return_val_if_fail (closure != NULL, NULL);

	task = g_object_new (IRIS_TYPE_TASK, NULL);

	if (G_LIKELY (task->priv->closure))
		g_closure_unref (task->priv->closure);
	task->priv->closure = g_closure_ref (closure);

	return task;
}

/**
 * iris_task_run:
 * @task: An #IrisTask
 *
 * Asynchronously schedules the task for execution.
 */
void
iris_task_run (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	msg = iris_message_new (IRIS_TASK_MESSAGE_EXECUTE);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_run_async:
 * @task: An #IrisTask
 * @callback: A #GAsyncReadyCallback
 * @user_data: data for @callback
 *
 * Asynchronously schedules the task for execution. Upon completion of
 * execution and callbacks/errbacks phase, @callback will be executed.
 */
void
iris_task_run_async (IrisTask            *task,
                     GAsyncReadyCallback  callback,
                     gpointer             user_data)
{
	IrisTaskPrivate    *priv;
	IrisMessage        *msg;
	GSimpleAsyncResult *res;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	res = g_simple_async_result_new (G_OBJECT (task),
	                                 callback, user_data,
	                                 (gpointer)G_STRFUNC);
	msg = iris_message_new_data (IRIS_TASK_MESSAGE_EXECUTE,
	                             G_TYPE_OBJECT, res);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_cancel:
 * @task: An #IrisTask
 *
 * Cancels a task.  If the task is already executing, it is up to the executing
 * task to periodically check the canceled state with iris_task_is_canceled()
 * and quit execution.
 */
void
iris_task_cancel (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	msg = iris_message_new (IRIS_TASK_MESSAGE_CANCEL);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_complete:
 * @task: An #IrisTask
 *
 * Marks the task as completing the execution phase.  This can be used by
 * asynchronous tasks to denote that they have completed.
 *
 * Completion of the task will result in the callbacks phase being performed.
 */
void
iris_task_complete (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	msg = iris_message_new (IRIS_TASK_MESSAGE_COMPLETE);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_add_callback:
 * @task: An #IrisTask
 * @callback: An #IrisTaskFunc
 * @user_data: user data for @callback
 * @notify: notify when the closure has executed
 *
 * Adds a callback to the callbacks phase.  The callback will be executed
 * in the order it was added.  If the task has an error when the callback
 * is reached, it will not be executed at all.
 */
void
iris_task_add_callback (IrisTask       *task,
                        IrisTaskFunc    callback,
                        gpointer        user_data,
                        GDestroyNotify  notify)
{
	GClosure *closure;

	g_return_if_fail (IRIS_IS_TASK (task));

	closure = g_cclosure_new (G_CALLBACK (callback),
	                          user_data,
	                          (GClosureNotify)notify);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	iris_task_add_callback_closure (task, closure);
	g_closure_unref (closure);
}

/**
 * iris_task_add_errback:
 * @task: An #IrisTask
 * @errback: An #IrisTaskFunc
 * @user_data: user data for @callback
 * @notify: notify when the closure has executed
 *
 * Adds an errback to the callbacks phase.  The errback will be executed
 * in the order it was added.  If errback will only execute if the task
 * has an error when the errback is reached.
 */
void
iris_task_add_errback (IrisTask       *task,
                       IrisTaskFunc    errback,
                       gpointer        user_data,
                       GDestroyNotify  notify)
{
	GClosure *closure;

	g_return_if_fail (IRIS_IS_TASK (task));

	closure = g_cclosure_new (G_CALLBACK (errback),
	                          user_data,
	                          (GClosureNotify)notify);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	iris_task_add_errback_closure (task, closure);
	g_closure_unref (closure);
}

/**
 * iris_task_add_both:
 * @task: An #IrisTask
 * @callback: An #IrisTaskFunc
 * @errback: An #IrisTaskFunc
 * @user_data: user data for @callback or @errback
 * @notify: A #GDestroyNotify
 *
 * Adds a new task handler to the callbacks phase of the task.  If the task
 * is in an errored state when the handler is reached, @errback will be
 * invoked.  Otherwise, @callback will be invoked.  One, and only one, of
 * these functions is guaranteed to be invoked.
 */
void
iris_task_add_both (IrisTask       *task,
                    IrisTaskFunc    callback,
                    IrisTaskFunc    errback,
                    gpointer        user_data,
                    GDestroyNotify  notify)
{
	GClosure *callback_closure,
	         *errback_closure;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (callback != NULL);
	g_return_if_fail (errback != NULL);

	callback_closure = g_cclosure_new (G_CALLBACK (callback), user_data, (GClosureNotify)notify);
	errback_closure = g_cclosure_new (G_CALLBACK (errback), user_data, (GClosureNotify)notify);
	g_closure_set_marshal (callback_closure, g_cclosure_marshal_VOID__VOID);
	g_closure_set_marshal (errback_closure, g_cclosure_marshal_VOID__VOID);
	iris_task_add_both_closure (task, callback_closure, errback_closure);
	g_closure_unref (callback_closure);
	g_closure_unref (errback_closure);
}

/**
 * iris_task_add_callback_closure:
 * @task: An #IrisTask
 * @closure: A #GClosure
 *
 * Adds a callback closure to be executed in the callbacks phase.
 *
 * See iris_task_add_callback().
 */
void
iris_task_add_callback_closure (IrisTask *task,
                                GClosure *closure)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;
	IrisTaskHandler *handler;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (closure != NULL);

	priv = task->priv;

	handler = g_slice_new0 (IrisTaskHandler);
	handler->callback = g_closure_ref (closure);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_HANDLER,
	                             G_TYPE_POINTER, handler);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_add_errback_closure:
 * @task: An #IrisTask
 * @closure: A #GClosure
 *
 * Adds an errback closure to be executed in the callbacks phase.
 *
 * See iris_task_add_errback().
 */
void
iris_task_add_errback_closure  (IrisTask *task,
                                GClosure *closure)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;
	IrisTaskHandler *handler;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (closure != NULL);

	priv = task->priv;

	handler = g_slice_new0 (IrisTaskHandler);
	handler->errback = g_closure_ref (closure);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_HANDLER,
	                             G_TYPE_POINTER, handler);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_add_both_closure:
 * @task: An #IrisTask
 * @callback: A #GClosure
 * @errback: A #GClosure
 *
 * Adds a task handler to the end of the callbacks chain.  If the task is
 * in an errored state when the task handler is reached, @errback will be
 * invoked.  Otherwise, @callback will be invoked.  One, and only one, of
 * the closures is guaranteed to be invoked.
 */
void
iris_task_add_both_closure (IrisTask *task,
                            GClosure *callback,
                            GClosure *errback)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;
	IrisTaskHandler *handler;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (callback != NULL || errback != NULL);

	priv = task->priv;

	handler = g_slice_new0 (IrisTaskHandler);
	handler->callback = g_closure_ref (callback);
	handler->errback = g_closure_ref (errback);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_HANDLER,
	                             G_TYPE_POINTER, handler);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_add_dependency:
 * @task: An #IrisTask
 * @dependency: An #IrisTask
 *
 * Prevents further execution of the task or callbacks phase until
 * the @dependency task has completed.
 */
void
iris_task_add_dependency (IrisTask *task,
                          IrisTask *dependency)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (IRIS_IS_TASK (dependency));

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_DEPENDENCY,
 	                             IRIS_TYPE_TASK, dependency);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_remove_dependency:
 * @task: An #IrisTask
 * @dependency: An #IrisTask
 *
 * Removes @dependency from preventing the tasks execution.  If the task is
 * ready to execute it will be scheduled for execution.
 */
void
iris_task_remove_dependency    (IrisTask *task,
                                IrisTask *dependency)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (IRIS_IS_TASK (dependency));

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_REMOVE_DEPENDENCY,
 	                             IRIS_TYPE_TASK, dependency);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_is_async:
 * @task: An #IrisTask
 *
 * Checks if the task is an asynchronous task, meaning it will not complete
 * when the tasks execute method returns.
 *
 * Return value: %TRUE if the task is asynchronous
 */
gboolean
iris_task_is_async (IrisTask *task)
{
	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);
	return FLAG_IS_ON (task, IRIS_TASK_FLAG_ASYNC);
}

/**
 * iris_task_is_executing:
 * @task: An #IrisTask
 *
 * Checks if the task is currently executing.
 *
 * Return value: %TRUE if the task is executing
 */
gboolean
iris_task_is_executing (IrisTask *task)
{
	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);
	return FLAG_IS_ON (task, IRIS_TASK_FLAG_EXECUTING);
}

/**
 * iris_task_is_canceled:
 * @task: An #IrisTask
 *
 * Checks if a task has been canceled.  Note that if the task handles
 * the cancel and chooses to ignore it, %FALSE will be returned.
 *
 * Return value: %TRUE if the task was canceled.
 */
gboolean
iris_task_is_canceled (IrisTask *task)
{
	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);
	return FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELED);
}

/**
 * iris_task_is_finished:
 * @task: An #IrisTask
 *
 * Checks to see if the task has executed and the callbacks phase has
 * completed.
 *
 * Return value: %TRUE if the task has completed
 */
gboolean
iris_task_is_finished (IrisTask *task)
{
	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);
	return FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED);
}

/**
 * iris_task_set_main_context:
 * @task: An #IrisTask
 * @context: A #GMainContext
 *
 * Sets a #GMainContext to use to perform the errbacks and callbacks
 * from.  All future callbacks and errbacks will be executed from within
 * the context.
 */
void
iris_task_set_main_context (IrisTask     *task,
                            GMainContext *context)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_CONTEXT,
	                             G_TYPE_POINTER, context);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_get_main_context:
 * @task: An #IrisTask
 *
 * Retrieves the #GMainContext associated with the task or %NULL.
 *
 * Return value: The #GMainContext or %NULL
 */
GMainContext*
iris_task_get_main_context (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_val_if_fail (IRIS_IS_TASK (task), NULL);

	priv = task->priv;

	return priv->context;
}

/**
 * iris_task_has_error:
 * @task: An #IrisTask
 *
 * Return value: %TRUE if the task is currently in an errored state.
 */
gboolean
iris_task_has_error (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	priv = task->priv;

	return (priv->error != NULL);
}

/**
 * iris_task_get_error:
 * @task: An #IrisTask
 * @error: A location for a #GError
 *
 * Stores a copy of the current error for the task into the location
 * @error.  If no error currently exists, the value stored will be %NULL.
 * The error must be freed by the caller using g_error_free().
 *
 * Return value: %TRUE if the task had an error and was copied.
 */
gboolean
iris_task_get_error (IrisTask *task,
                     GError   **error)
{
	IrisTaskPrivate *priv;
	gboolean         retval = FALSE;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);
	g_return_val_if_fail (error != NULL, FALSE);

	priv = task->priv;

	g_mutex_lock (priv->mutex);
	if (priv->error) {
		*error = g_error_copy (priv->error);
		retval = TRUE;
	}
	else {
		*error = NULL;
	}
	g_mutex_unlock (priv->mutex);

	return retval;
}

/**
 * iris_task_set_error:
 * @task: An #IrisTask
 * @error: A #GError
 *
 * Sets the error for the task.  If in the callback phase, the next iteration
 * will execute the errback instead of the callback.
 */
void
iris_task_set_error (IrisTask     *task,
                     const GError *error)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	g_mutex_lock (priv->mutex);
	if (priv->error)
		g_error_free (priv->error);
	if (error)
		priv->error = g_error_copy (error);
	else
		priv->error = NULL;
	g_mutex_unlock (priv->mutex);
}

/**
 * iris_task_take_error:
 * @task: An #IrisTask
 * @error: A #GError
 *
 * Steals the ownership of @error and attaches it to the task.
 */
void
iris_task_take_error (IrisTask *task,
                      GError   *error)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	g_mutex_lock (priv->mutex);
	if (priv->error)
		g_error_free (priv->error);
	priv->error = error;
	g_mutex_unlock (priv->mutex);
}

/**
 * iris_task_get_result:
 * @task: An #IrisTask
 * @value: A #GValue to store the current result
 *
 * Retreives the current value for the task and stores it to the
 * #GValue @value.
 */
void
iris_task_get_result (IrisTask *task,
                      GValue   *value)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (value != NULL);

	priv = task->priv;

	g_mutex_lock (priv->mutex);

	/* unset if there is a previous value */
	if (G_VALUE_TYPE (value) != G_TYPE_INVALID)
		g_value_unset (value);

	/* copy the type and real value */
	if (G_VALUE_TYPE (&priv->result) != G_TYPE_INVALID) {
		g_value_init (value, G_VALUE_TYPE (&priv->result));
		g_value_copy (&priv->result, value);
	}

	g_mutex_unlock (priv->mutex);
}

/**
 * iris_task_set_result:
 * @task: An #IrisTask
 * @value: A #GValue
 *
 * Sets the current result for the task.
 */
void
iris_task_set_result (IrisTask     *task,
                      const GValue *value)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	g_mutex_lock (priv->mutex);
	if (G_VALUE_TYPE (&priv->result) != G_TYPE_INVALID)
		g_value_unset (&priv->result);
	g_value_init (&priv->result, G_VALUE_TYPE (value));
	g_value_copy (value, &priv->result);
	g_mutex_unlock (priv->mutex);
}

/**
 * iris_task_set_result_gtype:
 * @task: An #IrisTask
 * @type: A #GType
 *
 * Sets the current value for the task without needing to use a #GValue
 * container.  You can pass a single value after @type.
 */
void
iris_task_set_result_gtype (IrisTask *task,
                            GType     type, ...)
{
	IrisTaskPrivate *priv;
	gchar           *error;
	va_list          args;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	g_mutex_lock (priv->mutex);

	va_start (args, type);
	if (G_VALUE_TYPE (&priv->result) != G_TYPE_INVALID)
		g_value_unset (&priv->result);
	g_value_init (&priv->result, type);
	G_VALUE_COLLECT (&priv->result, args, 0, &error);
	va_end (args);

	if (error) {
		g_warning ("%s: %s", G_STRFUNC, error);
		g_free (error);
		g_value_unset (&priv->result);
	}

	g_mutex_unlock (priv->mutex);
}

/**
 * iris_task_set_scheduler:
 * @task: An #IrisTask
 * @scheduler: An #IrisScheduler
 *
 * Sets the scheduler used to execute future work items.
 */
void
iris_task_set_scheduler (IrisTask      *task,
                         IrisScheduler *scheduler)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));

	priv = task->priv;

	iris_receiver_set_scheduler (priv->receiver, scheduler);
}

/**************************************************************************
 *                      IrisTask Private Implementation                   *
 *************************************************************************/

static void
iris_task_execute (IrisTask *task)
{
	g_return_if_fail (IRIS_IS_TASK (task));
	IRIS_TASK_GET_CLASS (task)->execute (task);
}

static void
iris_task_progress_callbacks_main (IrisTask *task)
{
	RUN_NEXT_HANDLER (task);
}

static void
iris_task_progress_callbacks_tick (IrisTask *task)
{
	IrisMessage *msg;

	/* The callbacks work by sending a message to push the callbacks
	 * progress continuely forward.  This way callbacks are able to
	 * pause further execution until other tasks have completed.
	 *
	 * Think of this as a psuedo tail-recursion, but with messages
	 * and not really tail-recursion at all :-)
	 */

	msg = iris_message_new (IRIS_TASK_MESSAGE_CALLBACKS);
	iris_port_post (task->priv->port, msg);
	iris_message_unref (msg);
}

static void
iris_task_progress_callbacks (IrisTask *task)
{
	IrisScheduler *scheduler;

	g_return_if_fail (IRIS_IS_TASK (task));

	if (G_UNLIKELY (task->priv->context)) {
		scheduler = IRIS_SCHEDULER (task->priv->context_sched);
		iris_scheduler_queue (scheduler,
		                      (IrisCallback)iris_task_progress_callbacks_main,
		                      task,
		                      (GDestroyNotify)iris_task_progress_callbacks_tick);
	}
	else {
		RUN_NEXT_HANDLER (task);
		iris_task_progress_callbacks_tick (task);
	}
}

static void
iris_task_notify_observers (IrisTask *task,
                            gboolean  canceled)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;
	GList           *iter;

	priv = task->priv;

	msg = iris_message_new_data (
			canceled ? IRIS_TASK_MESSAGE_DEP_CANCELED :
			           IRIS_TASK_MESSAGE_DEP_FINISHED,
			IRIS_TYPE_TASK, task);

	for (iter = priv->observers; iter; iter = iter->next)
		iris_port_post (IRIS_TASK (iter->data)->priv->port, msg);

	g_list_foreach (priv->observers, (GFunc)g_object_unref, NULL);
	g_list_free (priv->observers);
	priv->observers = NULL;

	iris_message_unref (msg);
}

static void
iris_task_complete_async_result (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	if (!priv->async_result)
		return;

	if (priv->context) {
		/* Queue into the main context */
		iris_scheduler_queue (IRIS_SCHEDULER (priv->context_sched),
		                      (IrisCallback)g_simple_async_result_complete,
		                      priv->async_result,
		                      NULL);
	}
	else {
		g_simple_async_result_complete ((GSimpleAsyncResult*)priv->async_result);
	}

	priv->async_result = NULL;
}

static void
iris_task_schedule (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	DISABLE_FLAG (task, IRIS_TASK_FLAG_NEED_EXECUTE);
	ENABLE_FLAG (task, IRIS_TASK_FLAG_EXECUTING);

	iris_scheduler_queue (priv->receiver->priv->scheduler,
	                      (IrisCallback)iris_task_execute,
	                      g_object_ref (task),
	                      NULL);
}

static void
iris_task_progress_or_finish (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *message;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	if (CAN_FINISH_NOW (task)) {
		message = iris_message_new (IRIS_TASK_MESSAGE_FINISH);
		iris_port_post (priv->port, message);
		iris_message_unref (message);
	}
	else {
		iris_task_progress_callbacks (task);
	}
}

/**************************************************************************
 *                    IrisTask Message Handling Methods                   *
 *************************************************************************/

static void
handle_complete (IrisTask    *task,
                 IrisMessage *message)
{
	g_return_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_EXECUTING));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_CALLBACKS));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_FINISHED));

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELED))
		return;

	DISABLE_FLAG (task, IRIS_TASK_FLAG_EXECUTING);
	ENABLE_FLAG (task, IRIS_TASK_FLAG_CALLBACKS);

	if (!PROGRESS_BLOCKED (task))
		iris_task_progress_callbacks (task);
}

static void
handle_cancel (IrisTask    *task,
               IrisMessage *message)
{
	IrisTaskPrivate *priv;
	gboolean         ignore;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	priv = task->priv;

	if (!(ignore = IRIS_TASK_GET_CLASS (task)->cancel (task))) {
		ENABLE_FLAG (task, IRIS_TASK_FLAG_CANCELED);
		ENABLE_FLAG (task, IRIS_TASK_FLAG_FINISHED);

		iris_task_notify_observers (task, TRUE);
		iris_task_complete_async_result (task);
	}
}

static void
handle_context (IrisTask    *task,
                IrisMessage *message)
{
	IrisTaskPrivate *priv;
	GMainContext    *context;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	priv = task->priv;
	context = g_value_get_pointer (iris_message_get_data (message));

	if (priv->context) {
		priv->context       = NULL;
		priv->context_sched = NULL; /* weak-ref */
	}

	if ((priv->context = context) != NULL) {
		GList *iter;

		for (iter = main_schedulers; iter; iter = iter->next) {
			if (iris_gmainscheduler_get_context (iter->data) == context) {
				priv->context_sched = iter->data;
				break;
			}
		}

		if (!priv->context_sched) {
			priv->context_sched = IRIS_GMAINSCHEDULER (iris_gmainscheduler_new (context));
			main_schedulers = g_list_prepend (main_schedulers, priv->context_sched);
		}
	}
}

static void
handle_execute (IrisTask    *task,
                IrisMessage *message)
{
	IrisTaskPrivate *priv;
	const GValue    *value = NULL;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_EXECUTING));

	priv = task->priv;
	value = iris_message_get_data (message);

	if (G_VALUE_TYPE (value) == G_TYPE_OBJECT)
		priv->async_result = g_value_get_object (value);

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELED))
		return;

	ENABLE_FLAG (task, IRIS_TASK_FLAG_NEED_EXECUTE);

	if (!PROGRESS_BLOCKED (task))
		iris_task_schedule (task);
}

static void
handle_callbacks (IrisTask    *task,
                  IrisMessage *message)
{
	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_EXECUTING) || FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS));

	if (!PROGRESS_BLOCKED (task))
		iris_task_progress_or_finish (task);
}

static void
handle_finish (IrisTask    *task,
               IrisMessage *message)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_EXECUTING) || FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS));

	priv = task->priv;

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED))
		return;

	if (PROGRESS_BLOCKED (task))
		return;

	if (priv->handlers) {
		iris_task_progress_or_finish (task);
		return;
	}

	ENABLE_FLAG (task, IRIS_TASK_FLAG_FINISHED);

	iris_task_notify_observers (task, FALSE);
	iris_task_complete_async_result (task);
}

static void
handle_add_handler (IrisTask    *task,
                    IrisMessage *message)
{
	IrisTaskPrivate *priv;
	IrisTaskHandler *handler;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	priv = task->priv;

	if (!(handler = g_value_get_pointer (iris_message_get_data (message))))
		return;

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELED))
		return;

	priv->handlers = g_list_append (priv->handlers, handler);

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED)) {
		ENABLE_FLAG (task, IRIS_TASK_FLAG_CALLBACKS);
		DISABLE_FLAG (task, IRIS_TASK_FLAG_FINISHED);
		iris_task_progress_or_finish (task);
	}
}

static void
handle_dep_finished (IrisTask    *task,
                     IrisMessage *message)
{
	IrisTaskClass *task_class;
	IrisTask      *dep;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (PROGRESS_BLOCKED (task));

	task_class = IRIS_TASK_GET_CLASS (task);
	dep = IRIS_TASK (g_value_get_object (iris_message_get_data (message)));

	task_class->dependency_finished (task, dep);

	if (!PROGRESS_BLOCKED (task)) {
		if (FLAG_IS_ON (task, IRIS_TASK_FLAG_NEED_EXECUTE))
			iris_task_schedule (task);
		else if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS))
			iris_task_progress_or_finish (task);
	}
}

static void
handle_dep_canceled (IrisTask    *task,
                     IrisMessage *message)
{
	IrisTaskClass *task_class;
	IrisTask      *dep;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (PROGRESS_BLOCKED (task));

	task_class = IRIS_TASK_GET_CLASS (task);
	dep = IRIS_TASK (g_value_get_object (iris_message_get_data (message)));

	task_class->dependency_canceled (task, dep);

	if (!PROGRESS_BLOCKED (task)) {
		if (FLAG_IS_ON (task, IRIS_TASK_FLAG_NEED_EXECUTE))
			iris_task_schedule (task);
		else if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS))
			iris_task_progress_or_finish (task);
	}
}

static void
handle_add_dependency (IrisTask    *task,
                       IrisMessage *message)
{
	IrisTaskPrivate *priv;
	IrisTask        *dep;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	priv = task->priv;
	dep = g_value_get_object (iris_message_get_data (message));

	priv->dependencies = g_list_prepend (priv->dependencies,
	                                     g_object_ref (dep));

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED)) {
		/* we were already done, lets move back to the
		 * callbacks phase and turn off the finished bit
		 */
		ENABLE_FLAG (task, IRIS_TASK_FLAG_CALLBACKS);
		DISABLE_FLAG (task, IRIS_TASK_FLAG_FINISHED);
	}

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_OBSERVER,
	                             IRIS_TYPE_TASK, task);
	iris_port_post (dep->priv->port, msg);
	iris_message_unref (msg);
}

static void
handle_remove_dependency (IrisTask    *task,
                          IrisMessage *message)
{
	IrisTaskPrivate *priv;
	IrisTask        *dep;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	priv = task->priv;
	dep = g_value_get_object (iris_message_get_data (message));
	iris_task_remove_dependency_sync (task, dep);
}

void
iris_task_remove_dependency_sync (IrisTask *task,
                                  IrisTask *dep)
{
	IrisTaskPrivate *priv;
	GList           *link;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (IRIS_IS_TASK (dep));

	priv = task->priv;

	if ((link = g_list_find (priv->dependencies, dep)) != NULL) {
		priv->dependencies = g_list_delete_link (priv->dependencies, link);
		g_object_unref (dep);
	}

	if (!PROGRESS_BLOCKED (task)) {
		if (FLAG_IS_ON (task, IRIS_TASK_FLAG_NEED_EXECUTE))
			iris_task_schedule (task);
		else if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS))
			iris_task_progress_or_finish (task);
	}
}

static void
handle_add_observer (IrisTask    *task,
                     IrisMessage *message)
{
	IrisTaskPrivate *priv;
	IrisTask        *observed;
	gboolean         cancel;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	priv = task->priv;
	observed = g_value_get_object (iris_message_get_data (message));

	priv->observers = g_list_prepend (priv->observers,
	                                  g_object_ref (observed));

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED)) {
		cancel = FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELED);
		iris_task_notify_observers (task, cancel);
	}
}

static void
iris_task_handle_message_real (IrisTask    *task,
                               IrisMessage *message)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	switch (message->what) {
	case IRIS_TASK_MESSAGE_CANCEL:
		handle_cancel (task, message);
		break;
	case IRIS_TASK_MESSAGE_CONTEXT:
		handle_context (task, message);
		break;
	case IRIS_TASK_MESSAGE_EXECUTE:
		handle_execute (task, message);
		break;
	case IRIS_TASK_MESSAGE_COMPLETE:
		handle_complete (task, message);
		break;
	case IRIS_TASK_MESSAGE_CALLBACKS:
		handle_callbacks (task,message);
		break;
	case IRIS_TASK_MESSAGE_FINISH:
		handle_finish (task, message);
		break;
	case IRIS_TASK_MESSAGE_ADD_HANDLER:
		handle_add_handler (task, message);
		break;
	case IRIS_TASK_MESSAGE_DEP_FINISHED:
		handle_dep_finished (task, message);
		break;
	case IRIS_TASK_MESSAGE_DEP_CANCELED:
		handle_dep_canceled (task, message);
		break;
	case IRIS_TASK_MESSAGE_ADD_DEPENDENCY:
		handle_add_dependency (task, message);
		break;
	case IRIS_TASK_MESSAGE_REMOVE_DEPENDENCY:
		handle_remove_dependency (task, message);
		break;
	case IRIS_TASK_MESSAGE_ADD_OBSERVER:
		handle_add_observer (task, message);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

/**************************************************************************
 *                 IrisTask Class VTable Implementations                  *
 *************************************************************************/

static void
iris_task_dependency_canceled_real (IrisTask *task,
                                    IrisTask *dependency)
{
	iris_task_cancel (task);
}

static void
iris_task_dependency_finished_real (IrisTask *task,
                                    IrisTask *dependency)
{
	iris_task_remove_dependency (task, dependency);
}

static void
iris_task_handle_message (IrisMessage *message,
                          gpointer     data)
{
	IrisTask *task;

	g_return_if_fail (IRIS_IS_TASK (data));

	task = IRIS_TASK (data);
	IRIS_TASK_GET_CLASS (task)->handle_message (task, message);
}

static gboolean
iris_task_cancel_real (IrisTask *task)
{
	return FALSE;
}

static void
iris_task_execute_real (IrisTask *task)
{
	GValue params = {0,};

	g_value_init (&params, G_TYPE_OBJECT);
	g_value_set_object (&params, task);
	g_closure_invoke (task->priv->closure, NULL, 1, &params, NULL);
	g_closure_invalidate (task->priv->closure);
	g_closure_unref (task->priv->closure);
	task->priv->closure = NULL;
	g_value_unset (&params);

	if (FLAG_IS_OFF (task, IRIS_TASK_FLAG_ASYNC))
		iris_task_complete (task);
}

static void
iris_task_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_task_parent_class)->finalize (object);
}

static void
iris_task_class_init (IrisTaskClass *task_class)
{
	GObjectClass *object_class;

	task_class->handle_message = iris_task_handle_message_real;
	task_class->cancel = iris_task_cancel_real;
	task_class->execute = iris_task_execute_real;
	task_class->dependency_finished = iris_task_dependency_finished_real;
	task_class->dependency_canceled = iris_task_dependency_canceled_real;

	object_class = G_OBJECT_CLASS (task_class);
	object_class->finalize = iris_task_finalize;

	g_type_class_add_private (object_class, sizeof(IrisTaskPrivate));
}

static void
iris_task_init (IrisTask *task)
{
	IrisTaskPrivate *priv;
	GClosure        *closure;

	priv = task->priv = IRIS_TASK_GET_PRIVATE (task);

	priv->port = iris_port_new ();
	priv->receiver = iris_arbiter_receive (iris_scheduler_default (),
	                                       priv->port,
	                                       iris_task_handle_message,
	                                       g_object_ref (task),
	                                       (GDestroyNotify)g_object_unref);
	priv->mutex = g_mutex_new ();

	/* FIXME: We should have a teardown port for dispose */
	iris_arbiter_coordinate (priv->receiver, NULL, NULL);

	/* default closure so we can conform to new style constructors */
	closure = g_cclosure_new (G_CALLBACK (iris_task_dummy), NULL, NULL);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	task->priv->closure = closure;
}

static void
iris_task_dummy (IrisTask *task, gpointer user_data)
{
}

/*************************************************************************
 *                        IrisTaskHandler API                            *
 *************************************************************************/

static void
iris_task_handler_free (IrisTaskHandler *handler)
{
	if (handler->callback) {
		g_closure_invalidate (handler->callback);
		g_closure_unref (handler->callback);
	}

	if (handler->errback) {
		g_closure_invalidate (handler->errback);
		g_closure_unref (handler->errback);
	}

	g_slice_free (IrisTaskHandler, handler);
}

static IrisTaskHandler*
iris_task_next_handler (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisTaskHandler *handler = NULL;

	priv = task->priv;

	if (priv->handlers) {
		handler = g_list_first (priv->handlers)->data;
		priv->handlers = g_list_remove (priv->handlers, handler);
	}

	return handler;
}

static IrisTaskHandler*
iris_task_next_callback (IrisTask *task)
{
	IrisTaskHandler *handler;

	while ((handler = iris_task_next_handler (task)) != NULL) {
		if (handler->callback)
			break;
		iris_task_handler_free (handler);
	}

	return handler;
}

static IrisTaskHandler*
iris_task_next_errback (IrisTask *task)
{
	IrisTaskHandler *handler;

	while ((handler = iris_task_next_handler (task)) != NULL) {
		if (handler->errback)
			break;
		iris_task_handler_free (handler);
	}

	return handler;
}
