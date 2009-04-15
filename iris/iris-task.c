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

#include "iris-task.h"
#include "iris-task-private.h"

G_DEFINE_TYPE (IrisTask, iris_task, G_TYPE_OBJECT);

typedef struct
{
	GClosure *callback;
	GClosure *errback;
} IrisTaskHandler;

static void
iris_task_dummy (IrisTask *task,
                 gpointer  user_data)
{
}

/**
 * iris_task_new:
 * @func: An #IrisTaskFunc to execute
 * @user_data: user data for @func
 * @notify: An optional #GDestroyNotify or %NULL
 *
 * Create a new #IrisTask instance.
 *
 * Return value: the newly created #IrisTask instance
 */
IrisTask*
iris_task_new (IrisTaskFunc   func,
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
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__OBJECT);
	task = iris_task_new_from_closure (closure);
	g_closure_unref (closure);

	return task;
}

/**
 * iris_task_new_from_closure:
 * @closure: A #GClosure
 *
 * Creates a new task using the closure for execution.
 *
 * Return value: the newly created #IrisTask
 */
IrisTask*
iris_task_new_from_closure (GClosure *closure)
{
	IrisTask *task;

	g_return_val_if_fail (closure != NULL, NULL);

	task = g_object_new (IRIS_TYPE_TASK, NULL);
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
iris_task_run (IrisTask            *task,
               GAsyncReadyCallback  callback)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_EXECUTE,
	                             G_TYPE_POINTER, callback);
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
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__OBJECT);
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
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__OBJECT);
	iris_task_add_errback_closure (task, closure);
	g_closure_unref (closure);
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

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (closure != NULL);

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_CALLBACK,
	                             G_TYPE_CLOSURE, closure);
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

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (closure != NULL);

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_ERRBACK,
	                             G_TYPE_CLOSURE, closure);
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
	glong flags;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	flags = g_atomic_int_get (&task->priv->flags);
	return (flags & IRIS_TASK_FLAG_ASYNC) != 0;
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
	glong flags;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	flags = g_atomic_int_get (&task->priv->flags);
	return (flags & IRIS_TASK_FLAG_EXECUTING) != 0;
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
	glong flags;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	flags = g_atomic_int_get (&task->priv->flags);
	return (flags & IRIS_TASK_FLAG_CANCELED) != 0;
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
	glong flags;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	flags = g_atomic_int_get (&task->priv->flags);
	return (flags & IRIS_TASK_FLAG_FINISHED) != 0;
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
 * iris_task_get_error:
 * @task: An #IrisTask
 *
 * Retrieves the current error for the task or %NULL if there is no error.
 *
 * Return value: The #GError instance that should not be modified or %NULL
 */
G_CONST_RETURN GError*
iris_task_get_error (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_val_if_fail (IRIS_IS_TASK (task), NULL);

	priv = task->priv;

	return g_atomic_pointer_get (&priv->error);
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
	IrisMessage     *msg;
	GError          *real_error = NULL;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	if (error)
		real_error = g_error_copy (error);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ERROR,
	                             G_TYPE_POINTER, real_error);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
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
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ERROR,
	                             G_TYPE_POINTER, error);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

/**
 * iris_task_get_result:
 * @task: An #IrisTask
 *
 * Retreives the current value for the task.  This method is not thread
 * safe as the executing thread could modify the value out from under
 * you.  It is safe however from within the task's execution thread.
 *
 * Return value: The current result which should not be modified or freed.
 */
G_CONST_RETURN GValue*
iris_task_get_result (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_val_if_fail (IRIS_IS_TASK (task), NULL);

	priv = task->priv;

	return &priv->result;
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
	IrisMessage     *msg;
	GValue          *val;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	val = g_slice_new0 (GValue);
	g_value_init (val, G_VALUE_TYPE (value));
	g_value_copy (value, val);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_RESULT,
	                             G_TYPE_POINTER, val);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
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
	IrisMessage     *msg;
	GValue          *val;
	va_list          args;
	gchar           *error;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	val = g_slice_new0 (GValue);
	g_value_init (val, type);

	va_start (args, type);
	G_VALUE_COLLECT (val, args, 0, &error);
	va_end (args);

	if (error) {
		g_warning ("%s: %s", G_STRFUNC, error);
		g_free (error);
		g_value_unset (val);
	}

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_RESULT,
	                             G_TYPE_POINTER, val);
	iris_port_post (priv->port, msg);
	iris_message_unref (msg);
}

static void
iris_task_finalize (GObject *object)
{
	G_OBJECT_CLASS (iris_task_parent_class)->finalize (object);
}

static void
iris_task_class_init (IrisTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	object_class->finalize = iris_task_finalize;

	g_type_class_add_private (object_class, sizeof(IrisTaskPrivate));
}

static void
iris_task_init (IrisTask *self)
{
	self->priv = IRIS_TASK_GET_PRIVATE (self);
}
