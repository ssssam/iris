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
#include "iris-receiver-private.h"

#define CAN_EXECUTE_NOW(t)                                      \
	(((t->priv->flags & IRIS_TASK_FLAG_EXECUTING) == 0) &&  \
	 ((t->priv->flags & IRIS_TASK_FLAG_CALLBACKS) == 0) &&  \
	 (t->priv->dependencies == NULL))
#define CAN_CALLBACK_NOW(t)                                     \
	((((t->priv->dependencies == NULL)                   && \
	 (((t->priv->flags & IRIS_TASK_FLAG_EXECUTING) != 0) || \
	   (t->priv->flags & IRIS_TASK_FLAG_CALLBACKS) != 0))))
#define CAN_FINISH_NOW(t)                                       \
	((t->priv->dependencies == NULL) &&                     \
	 (t->priv->handlers == NULL) &&				\
	 ((t->priv->flags & IRIS_TASK_FLAG_CALLBACKS) != 0))

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
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	task = iris_task_new_from_closure (closure);
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
	IrisTask *task = iris_task_new (func, user_data, notify);
	/* scheduler first so future messages pass over this scheduler */
	if (scheduler)
		iris_task_set_scheduler (task, scheduler);
	if (async)
		task->priv->flags |= IRIS_TASK_FLAG_ASYNC;
	if (context)
		iris_task_set_main_context (task, context);
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
 * iris_task_run_full:
 * @task: An #IrisTask
 * @callback: A #GAsyncReadyCallback
 * @user_data: data for @callback
 *
 * Asynchronously schedules the task for execution. Upon completion of
 * execution and callbacks/errbacks phase, @callback will be executed.
 */
void
iris_task_run_full (IrisTask            *task,
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
	                             G_TYPE_CLOSURE,
	                             g_closure_ref (closure));
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
	                             G_TYPE_CLOSURE,
	                             g_closure_ref (closure));
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

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;
	msg = iris_message_new (IRIS_TASK_MESSAGE_RESULT);

	g_value_init (&msg->data, G_VALUE_TYPE (value));
	g_value_copy (value, &msg->data);

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
	va_list          args;
	gchar           *error;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;
	msg = iris_message_new (IRIS_TASK_MESSAGE_RESULT);

	va_start (args, type);
	g_value_init (&msg->data, type);
	G_VALUE_COLLECT (&msg->data, args, 0, &error);
	va_end (args);

	if (error) {
		g_warning ("%s: %s", G_STRFUNC, error);
		g_free (error);
		g_value_unset (&msg->data);
		goto cleanup;
	}

	iris_port_post (priv->port, msg);

cleanup:
	iris_message_unref (msg);
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

static void
iris_task_execute_worker (gpointer data)
{
	g_return_if_fail (IRIS_IS_TASK (data));
	IRIS_TASK_GET_CLASS (data)->execute (IRIS_TASK (data));
}

static void
iris_task_execute_notify (gpointer data)
{
}

static void
iris_task_callbacks_worker (IrisTask *task)
{
	GMutex       *mutex;
	GCond        *cond;
	GMainContext *context;
	IrisMessage  *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	if (G_UNLIKELY ((context = task->priv->context) != NULL)) {
		mutex = task->priv->context_mutex;
		cond = task->priv->context_cond;

		g_mutex_lock (mutex);

	retry:
		if (!g_main_context_wait (context, cond, mutex))
			goto retry;

		// get the next callback

		g_mutex_unlock (mutex);
	}
	else {
		// get the next callback
	}

	/* send message for next callback iteration
	 * this allows for callbacks pausing execution for future
	 * tasks to complete before more callbacks
	 */
	msg = iris_message_new (IRIS_TASK_MESSAGE_CALLBACKS);
	iris_port_post (task->priv->port, msg);
	iris_message_unref (msg);
}

static void
iris_task_handle_message_real (IrisTask    *task,
                               IrisMessage *message)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	switch (message->what) {
	case IRIS_TASK_MESSAGE_ERROR: {
		GError *error = priv->error;
		priv->error = g_value_get_pointer (iris_message_get_data (message));
		if (error)
			g_error_free (error);
		break;
	}
	case IRIS_TASK_MESSAGE_RESULT: {
		if (G_VALUE_TYPE (&priv->result) != G_TYPE_INVALID)
			g_value_unset (&priv->result);
		g_value_init (&priv->result, G_VALUE_TYPE (iris_message_get_data (message)));
		g_value_copy (iris_message_get_data (message), &priv->result);
		break;
	}
	case IRIS_TASK_MESSAGE_CANCEL: {
		/* the class can handle cancel and deny it */
		if (!IRIS_TASK_GET_CLASS (task)->cancel (task)) {
			priv->flags |= IRIS_TASK_FLAG_CANCELED;
		}
		break;
	}
	case IRIS_TASK_MESSAGE_CONTEXT: {
		if (priv->context) {
			g_mutex_free (priv->context_mutex);
			g_cond_free (priv->context_cond);
			priv->context_mutex = NULL;
			priv->context_cond = NULL;
		}
		g_atomic_pointer_set (&priv->context,
		                      g_value_get_pointer (iris_message_get_data (message)));
		if (priv->context) {
			priv->context_mutex = g_mutex_new ();
			priv->context_cond = g_cond_new ();
		}
		break;
	}
	case IRIS_TASK_MESSAGE_EXECUTE: {
		/* if the execute message has an async result to complete,
		 * we need to store it for later execution.
		 */
		if (G_VALUE_TYPE (iris_message_get_data (message)) == G_TYPE_OBJECT)
			priv->async_result = g_value_get_object (iris_message_get_data (message));
		if (CAN_EXECUTE_NOW (task)) {
			g_atomic_int_set (&priv->flags,
			                  priv->flags |= IRIS_TASK_FLAG_EXECUTING);
			iris_scheduler_queue (priv->receiver->priv->scheduler,
			                      iris_task_execute_worker,
			                      g_object_ref (task),
			                      iris_task_execute_notify);
			/* Make sure we unref the task after
			 * execution/callbacks/notify has complated.
			 */
		}
		break;
	}
	case IRIS_TASK_MESSAGE_COMPLETE: {
		if (CAN_CALLBACK_NOW (task)) {
			g_atomic_int_set (&priv->flags,
			                  (priv->flags & ~IRIS_TASK_FLAG_EXECUTING) | IRIS_TASK_FLAG_CALLBACKS);
			/* start the callbacks phase */
			iris_task_callbacks_worker (task);
		}
		break;
	}
	case IRIS_TASK_MESSAGE_CALLBACKS: {
		if (CAN_CALLBACK_NOW (task)) {
			if (CAN_FINISH_NOW (task)) {
				IrisMessage *fmsg = iris_message_new (IRIS_TASK_MESSAGE_FINISH);
				iris_port_post (priv->port, fmsg);
				iris_message_unref (fmsg);
			}
			else {
				iris_task_callbacks_worker (task);
			}
		}
		break;
	}
	case IRIS_TASK_MESSAGE_FINISH: {
		if (CAN_FINISH_NOW (task)) {
			/* notify our observers that we are complete */
			/* FIXME: Notify each of our observers */

			/* finish our async result if we have one */
			if (priv->async_result) {
				if (priv->context)
					/* FIXME: complete within their context */
					g_simple_async_result_complete_in_idle ((GSimpleAsyncResult*)priv->async_result);
				else
					g_simple_async_result_complete ((GSimpleAsyncResult*)priv->async_result);
			}
		}
		break;
	}
	default:
		g_assert_not_reached ();
		break;
	}
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

	g_closure_invoke (task->priv->closure,
	                  NULL, 1, &params, NULL);

	g_closure_invalidate (task->priv->closure);
	g_closure_unref (task->priv->closure);
	task->priv->closure = NULL;
	g_value_unset (&params);

	/* if not async, we can notify that we are done */
	if ((task->priv->flags & IRIS_TASK_FLAG_ASYNC) == 0)
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

	object_class = G_OBJECT_CLASS (task_class);
	object_class->finalize = iris_task_finalize;

	g_type_class_add_private (object_class, sizeof(IrisTaskPrivate));
}

static void
iris_task_init (IrisTask *task)
{
	IrisTaskPrivate *priv;

	priv = task->priv = IRIS_TASK_GET_PRIVATE (task);

	priv->port = iris_port_new ();
	priv->receiver = iris_receiver_new_full (iris_scheduler_default (),
	                                         NULL, /* IrisArbiter */
	                                         iris_task_handle_message,
	                                         task);
	iris_port_set_receiver (priv->port, priv->receiver);
}
