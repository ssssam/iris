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

#include "iris-debug.h"
#include "iris-gmainscheduler.h"
#include "iris-receiver-private.h"
#include "iris-task.h"
#include "iris-task-private.h"

/**
 * SECTION:iris-task
 * @title: IrisTask
 * @short_description: A concurrent and asynchronous task abstraction
 *
 * #IrisTask is a single-shot work-item that performs an atomic piece of work.
 * An example of this could be retrieving the contents of a web-page, removing
 * a file from disk or generating a thumbnail of an image.  Iris will try to
 * schedule your work items across system resources in the most efficient way.
 *
 * Upon completion of tasks, a series of callbacks or errbacks can be executed.
 * Callbacks and errbacks can alter the current result of the task or even yield
 * new tasks that must be completed before further callbacks or errbacks can be
 * executed. The task cannot be cancelled once it has reached the callbacks
 * phase; this means you can be sure either all or none of the
 * callbacks/errbacks will run.
 *
 * An #IrisTask object will free itself when its work and post-processing have
 * completed or cancelled. If you want to keep it alive, you should add your own
 * reference with g_object_ref() before calling iris_task_run().
 *
 * <refsect2 id="message-passing">
 * <title>Message Passing</title>
 * <para>
 * #IrisTask's control methods do not perform actions directly. Instead, when for
 * example iris_task_run() is called, a message is sent to the process asking it
 * to start work and then the function returns. If you then call
 * iris_task_is_executing(), it may return %TRUE or %FALSE depending on if the
 * message has been processed. The advantage of this method is that you may call
 * #IrisTask functions from any thread you like. Messages are always executed in
 * the order that they are sent.
 * </para></refsect2>
 *
 * <refsect2 id="lifecycle">
 * <title>Lifecycle in detail</title>
 * <para>
 * The execution reference of #IrisTask is implemented using #GInitiallyUnowned.
 * This is essentially a utility feature for C programmers and is normally used
 * for objects which are consumed by another object, so they do not have to be
 * explicity freed by the user; a good example is #IrisMessage. In #IrisTask
 * the situation is slightly different. The task effectively consumes itself, by
 * calling g_object_ref_sink() when the task begins to execute and freeing the
 * reference when finished.
 *
 * The main side-effect of this is that if for some reason you need to free a
 * task even before it has been executed, you should still call
 * iris_task_cancel() rather than g_object_unref().
 * </para>
 * </refsect2>
 */

#define CAN_FINISH_NOW(t)                                           \
	((t->priv->dependencies == NULL) &&                             \
	 (t->priv->handlers == NULL) &&	                                \
	 ((t->priv->flags & IRIS_TASK_FLAG_CALLBACKS_ACTIVE) != 0))
#define RUN_NEXT_HANDLER(t)                                         \
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
#define PROGRESS_BLOCKED(t)                          \
          (t->priv->dependencies != NULL &&          \
           FLAG_IS_OFF (t, IRIS_TASK_FLAG_CANCELLED))

G_DEFINE_TYPE (IrisTask, iris_task, G_TYPE_INITIALLY_UNOWNED);

enum {
	PROP_0,
	PROP_CONTROL_SCHEDULER,
	PROP_WORK_SCHEDULER
};

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
 * @func: An #IrisTaskFunc to execute
 * @user_data: user data for @func
 * @notify: An optional #GDestroyNotify or %NULL
 *
 * Creates a new #IrisTask instance.
 *
 * Return value: the newly created #IrisTask instance
 */
IrisTask*
iris_task_new (IrisTaskFunc   func,
               gpointer       user_data,
               GDestroyNotify notify)
{
	return iris_task_new_full (func, user_data, notify, FALSE, NULL, NULL, NULL);
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
	return iris_task_new_with_closure_full (closure, FALSE, NULL, NULL, NULL);
}

/**
 * iris_task_new_full:
 * @func: An #IrisTaskFunc
 * @user_data: data for @func
 * @notify: A destroy notify after execution of the task
 * @async: %FALSE unless the task will not complete during the execution of
 *         @func, and will call iris_task_work_finished() later.
 * @control_scheduler: An #IrisScheduler, or %NULL to use the default
 * @work_scheduler: An #IrisScheduler or %NULL to use the default
 * @context: A #GMainContext or %NULL
 *
 * Creates a new instance of #IrisTask.  This method allows for setting
 * if the task is asynchronous with @async.  An asynchronous task has the
 * ability to not complete during the execution of the task's execution
 * method (in this case @func).  To mark the task's execution as completed,
 * iris_task_work_finished() must be called for the task.
 *
 * If you want errbacks and callbacks to complete within a #GMainContext,
 * you may specify @context or %NULL for the callbacks to happen within
 * the worker thread.
 *
 * @work_scheduler allows you to set a specific #IrisScheduler to perform
 * the task's work.  All message passing associated with the task's internal
 * #IrisPort will still happen on @control_scheduler. Passing %NULL for either
 * of these will use the defaults returned by iris_get_default_work_scheduler()
 * and iris_get_default_control_scheduler() respectively.
 *
 * Return value: The newly created #IrisTask instance.
 */
IrisTask*
iris_task_new_full (IrisTaskFunc   func,
                    gpointer       user_data,
                    GDestroyNotify notify,
                    gboolean       async,
                    IrisScheduler *control_scheduler,
                    IrisScheduler *work_scheduler,
                    GMainContext  *context)
{
	GClosure *closure;
	IrisTask *task;

	if (!func)
		func = iris_task_dummy;

	closure = g_cclosure_new (G_CALLBACK (func),
	                          user_data,
	                          (GClosureNotify)notify);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	task = iris_task_new_with_closure_full (closure,
	                                        async,
	                                        control_scheduler,
	                                        work_scheduler,
	                                        context);
	g_closure_unref (closure);

	return task;
}

/**
 * iris_task_new_with_closure_full
 * @closure: A #GClosure
 * @async: %FALSE unless the task will not complete during the execution of
 *         @closure, and will call iris_task_work_finished() later.
 * @control_scheduler: An #IrisScheduler, or %NULL
 * @work_scheduler: An #IrisScheduler, or %NULL
 * @context: A #GMainContext, or %NULL
 *
 * A version of iris_task_new_full() that takes a #GClosure.
 *
 * Return value: a newly-allocated #IrisTask object.
 */
IrisTask *
iris_task_new_with_closure_full (GClosure      *closure,
                                 gboolean       async,
                                 IrisScheduler *control_scheduler,
                                 IrisScheduler *work_scheduler,
                                 GMainContext  *context)
{
	IrisTask *task;

	task = g_object_new (IRIS_TYPE_TASK,
	                     "control-scheduler", control_scheduler,
	                     "work-scheduler", work_scheduler,
	                     NULL);

	/* The closure is unreferenced in iris_task_execute_real() after being run */
	task->priv->closure = g_closure_ref (closure);

	if (async)
		task->priv->flags |= IRIS_TASK_FLAG_ASYNC;

	if (context)
		iris_task_set_main_context (task, context);

	return task;
}

/**
 * iris_task_run:
 * @task: An #IrisTask
 *
 * Asynchronously schedules the task for execution.
 *
 * When the task runs, g_object_ref_sink() will be called to sink or give the
 * task its <firstterm>execution reference</firstterm>. This is a reference that
 * will persist until the task completes or is canceled, enabling the task to be
 * automatically freed if and when it is no longer needed.
 *
 * If @task was already cancelled, this function will do nothing, unless the
 * caller doesn't hold a reference on @task in which case there will probably
 * be a segfault as @task would have been freed when it was cancelled.
 */
void
iris_task_run (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (! iris_task_is_executing (task));

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED))
		return;

	priv = task->priv;

	msg = iris_message_new (IRIS_TASK_MESSAGE_START_WORK);
	iris_port_post (priv->port, msg);
}

/**
 * iris_task_run_with_async_result:
 * @task: An #IrisTask
 * @callback: A #GAsyncReadyCallback
 * @user_data: data for @callback
 *
 * Asynchronously schedules the task for execution. Upon completion of
 * execution and callbacks/errbacks phase, @callback will be executed.
 */
void
iris_task_run_with_async_result (IrisTask            *task,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
	IrisTaskPrivate    *priv;
	IrisMessage        *msg;
	GSimpleAsyncResult *res;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (! iris_task_is_executing (task));

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED))
		return;

	priv = task->priv;

	res = g_simple_async_result_new (G_OBJECT (task),
	                                 callback, user_data,
	                                 (gpointer)G_STRFUNC);
	msg = iris_message_new_data (IRIS_TASK_MESSAGE_START_WORK,
	                             G_TYPE_OBJECT, res);
	iris_port_post (priv->port, msg);
}

/**
 * iris_task_cancel:
 * @task: An #IrisTask
 *
 * Requests a task to cancel. If the task is already executing, it is up to the
 * executing task to periodically check the cancelled state with
 * iris_task_is_cancelled() and quit execution. If the work function has
 * already completed the cancel will be ignored and the callbacks phase will
 * complete in full.
 *
 * When @task has fully cancelled it will be destroyed, unless still referenced
 * elsewhere. Because an #IrisTask has a floating reference until it runs, if
 * the task has not yet executed this reference will be sunk and then released.
 */
void
iris_task_cancel (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE) ||
	    FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED)) {
		/* Too late to cancel. We check for this again in the message handler
		 * because the situation could change by then */
		return;
	}

	priv = task->priv;

	msg = iris_message_new (IRIS_TASK_MESSAGE_START_CANCEL);
	iris_port_post (priv->port, msg);
}

/**
 * iris_task_work_finished:
 * @task: An #IrisTask
 *
 * Sends a message to the task to finish the work execution phase. This should
 * be used by tasks created with the 'async' flag set to signal that the work
 * has completed or signalled an error.
 *
 * Finishing the work starts the callbacks phase executing. The task is not
 * considered fully executed until the callbacks/errbacks have finished.
 */
/* FIXME: surely set_fatal_error should stop execution, so that they don't need
 * to call here in that situation? In which case this function could go back to
 * being called _completed.
 */
void
iris_task_work_finished (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_WORK_ACTIVE));

	priv = task->priv;

	msg = iris_message_new (IRIS_TASK_MESSAGE_WORK_FINISHED);
	iris_port_post (priv->port, msg);
}

/**
 * iris_task_set_progress_mode:
 * @task: An #IrisTask
 * @mode: An #IrisProgressMode value
 *
 * Sets the display mode for progress monitors that will watch @task. See
 * #IrisProgressMode for more information. This value is immutable once
 * iris_task_run() is called.
 *
 * Unlike other control methods, this function updates the value immediately.
 */
void
iris_task_set_progress_mode (IrisTask         *task,
                             IrisProgressMode  mode)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;

	/* FIXME: this should be immutable once ACTIVE */
	priv->progress_mode = mode;
}

/**
 * iris_task_get_progress_mode:
 * @task: An #IrisTask
 *
 * Returns @task's progress mode.
 *
 * Return value: the #IrisProgressMode that should be used to display @task.
 */
IrisProgressMode
iris_task_get_progress_mode (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_val_if_fail (IRIS_IS_TASK (task), 0);

	priv = task->priv;

	return priv->progress_mode;
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
 *
 * Callbacks cannot be added once the task has started executing.
 */
void
iris_task_add_callback (IrisTask       *task,
                        IrisTaskFunc    callback,
                        gpointer        user_data,
                        GDestroyNotify  notify)
{
	GClosure *closure;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

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
 *
 * Callbacks cannot be added once the task has started executing.
 */
void
iris_task_add_errback (IrisTask       *task,
                       IrisTaskFunc    errback,
                       gpointer        user_data,
                       GDestroyNotify  notify)
{
	GClosure *closure;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

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
 *
 * Callbacks cannot be added once the task has started executing.
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
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

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
 * Adds a callback closure to be executed in the callbacks phase. Callbacks
 * cannot be added once the task has started executing.
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
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;

	handler = g_slice_new0 (IrisTaskHandler);
	handler->callback = g_closure_ref (closure);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_HANDLER,
	                             G_TYPE_POINTER, handler);
	iris_port_post (priv->port, msg);
}

/**
 * iris_task_add_errback_closure:
 * @task: An #IrisTask
 * @closure: A #GClosure
 *
 * Adds an errback closure to be executed in the callbacks phase. Callbacks
 * cannot be added once the task has started executing.
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
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;

	handler = g_slice_new0 (IrisTaskHandler);
	handler->errback = g_closure_ref (closure);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_HANDLER,
	                             G_TYPE_POINTER, handler);
	iris_port_post (priv->port, msg);
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
 *
 * Callbacks cannot be added once the task has started executing.
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
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;

	handler = g_slice_new0 (IrisTaskHandler);
	handler->callback = g_closure_ref (callback);
	handler->errback = g_closure_ref (errback);

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_HANDLER,
	                             G_TYPE_POINTER, handler);
	iris_port_post (priv->port, msg);
}

/**
 * iris_task_add_dependency:
 * @task: An #IrisTask
 * @dependency: An #IrisTask
 *
 * Prevents execution of the task until @dependency has completed. This function
 * can only be called before the task has started executing.
 */
void
iris_task_add_dependency (IrisTask *task,
                          IrisTask *dependency)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (IRIS_IS_TASK (dependency));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_DEPENDENCY,
	                             IRIS_TYPE_TASK, dependency);
	iris_port_post (priv->port, msg);
}

/**
 * iris_task_is_async:
 * @task: An #IrisTask
 *
 * Checks if the task is an asynchronous task, meaning it will not complete
 * automatically when the tasks execute method returns.
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

	return FLAG_IS_ON (task, IRIS_TASK_FLAG_WORK_ACTIVE) ||
	       FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE);
}

/**
 * iris_task_is_finished:
 * @task: An #IrisTask
 *
 * Checks if a task has completed, raised a fatal error or been cancelled. If
 * not cancelled, the task is considered finished once the callbacks or errbacks
 * have all executed.
 *
 * Return value: %TRUE if no more work can be done
 */
gboolean
iris_task_is_finished (IrisTask *task)
{
	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	return FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED);
}

/**
 * iris_task_has_succeeded:
 * @task: An #IrisTask
 *
 * Checks to see if the task has executed sucessfully.
 *
 * Return value: %TRUE if the task completed without cancelling or
 *               raising a fatal error.
 */
gboolean
iris_task_has_succeeded (IrisTask *task)
{
	return IRIS_TASK_GET_CLASS (task)->has_succeeded (task);
}

/**
 * iris_task_has_failed:
 * @task: An #IrisTask
 *
 * Return value: %TRUE if the task raised a fatal error.
 */
gboolean
iris_task_has_failed (IrisTask *task)
{
	return IRIS_TASK_GET_CLASS (task)->has_failed (task);
}

/**
 * iris_task_is_cancelled:
 * @task: An #IrisTask
 *
 * Checks if a task has been cancelled.  Note that if the task handles
 * the cancel and chooses to ignore it, %FALSE will be returned. Similarly, if
 * a task receives the cancel after the work function completes, %FALSE will be
 * returned.
 *
 * Return value: %TRUE if the task was cancelled.
 */
gboolean
iris_task_is_cancelled (IrisTask *task)
{
	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);
	return FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED);
}

/**
 * iris_task_get_fatal_error:
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
iris_task_get_fatal_error (IrisTask *task,
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
 * iris_task_set_fatal_error:
 * @task: An #IrisTask
 * @error: A #GError
 *
 * Sets the error for the task.  If in the callback phase, the next iteration
 * will execute the errback instead of the callback.
 */
/* FIXME: abort the task when a fatal error is raised :) */
void
iris_task_set_fatal_error (IrisTask     *task,
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
 * iris_task_take_fatal_error:
 * @task: An #IrisTask
 * @error: A #GError
 *
 * Steals the ownership of @error and attaches it to the task.
 */
void
iris_task_take_fatal_error (IrisTask *task,
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
 * @Varargs: the value in the format appropriate for @type
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
 * iris_task_set_main_context:
 * @task: An #IrisTask
 * @context: A #GMainContext
 *
 * Sends a message to @task with a #GMainContext to run the errbacks and
 * callbacks from. This must be called before @task has begun execution.
 *
 * This function will <emphasis>not</emphasis> cause the actual task to execute
 * in the GMainContext; if you do want to achieve this you should use
 * iris_task_new_full() and pass an #IrisGMainScheduler as the work scheduler.
 * However, consider if you actually need #IrisTask if you don't need the task to
 * run in parallel - it might be sufficient to simply enqueue your function for
 * execution using g_idle_add().
 */
void
iris_task_set_main_context (IrisTask     *task,
                            GMainContext *context)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_SET_MAIN_CONTEXT,
	                             G_TYPE_POINTER, context);
	iris_port_post (priv->port, msg);
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

	msg = iris_message_new (IRIS_TASK_MESSAGE_PROGRESS_CALLBACKS);
	iris_port_post (task->priv->port, msg);
}

static void
iris_task_progress_callbacks_main (IrisTask *task)
{
	RUN_NEXT_HANDLER (task);
	iris_task_progress_callbacks_tick (task);
}

void
iris_task_progress_callbacks (IrisTask *task)
{
	IrisScheduler *scheduler;

	g_return_if_fail (IRIS_IS_TASK (task));

	if (G_UNLIKELY (task->priv->context)) {
		scheduler = IRIS_SCHEDULER (task->priv->context_sched);
		iris_scheduler_queue (scheduler,
		                      (IrisCallback)iris_task_progress_callbacks_main,
		                      task,
		                      NULL);
	}
	else {
		RUN_NEXT_HANDLER (task);
		iris_task_progress_callbacks_tick (task);
	}
}

void
iris_task_notify_observers (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *msg;
	GList           *iter;

	priv = task->priv;

	#ifdef IRIS_TRACE_TASK
	g_print ("%lx: Sending %s to %i observers\n", (gulong)task,
	         iris_task_is_cancelled (task)? "canceled": "completed",
	         g_list_length (priv->observers));
	#endif

	if (priv->observers == NULL)
		return;

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED))
		msg = iris_message_new_data (IRIS_TASK_MESSAGE_DEP_CANCELLED,
		                             IRIS_TYPE_TASK, task);
	else
	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED))
		msg = iris_message_new_data (IRIS_TASK_MESSAGE_DEP_FINISHED,
		                             IRIS_TYPE_TASK, task);
	else
		g_warn_if_reached ();

	iris_message_ref (msg);

	for (iter = priv->observers; iter; iter = iter->next)
		iris_port_post (IRIS_TASK (iter->data)->priv->port, msg);

	iris_message_unref (msg);

	g_list_free (priv->observers);
	priv->observers = NULL;
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
		                      g_object_unref);
	}
	else {
		g_simple_async_result_complete ((GSimpleAsyncResult*)priv->async_result);
		g_object_unref (priv->async_result);
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
	ENABLE_FLAG (task, IRIS_TASK_FLAG_WORK_ACTIVE);

	iris_scheduler_queue (g_atomic_pointer_get (&priv->work_scheduler),
	                      (IrisCallback)iris_task_execute,
	                      task,
	                      NULL);
}

static void
iris_task_progress_callbacks_or_finish (IrisTask *task)
{
	IrisTaskPrivate *priv;
	IrisMessage     *message;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	if (CAN_FINISH_NOW (task)) {
		message = iris_message_new (IRIS_TASK_MESSAGE_CALLBACKS_FINISHED);
		iris_port_post (priv->port, message);
	}
	else {
		iris_task_progress_callbacks (task);
	}
}

static void
iris_task_remove_dependency (IrisTask *task,
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
}

void
iris_task_remove_dependency_sync (IrisTask *task,
                                  IrisTask *dependency)
{
	IrisTaskPrivate *priv;
	IrisTask        *dep;
	IrisMessage     *message;
	GList           *node;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (IRIS_IS_TASK (dependency));

	priv = task->priv;

	if ((node = g_list_find (priv->dependencies, dependency)) != NULL) {
		dep = IRIS_TASK (node->data);

		if (FLAG_IS_OFF (dep, IRIS_TASK_FLAG_FINISHED)) {
			message = iris_message_new_data (IRIS_TASK_MESSAGE_REMOVE_OBSERVER,
			                                 IRIS_TYPE_TASK, task);
			iris_port_post (dep->priv->port, message);
		}

		priv->dependencies = g_list_delete_link (priv->dependencies, node);
		g_object_unref (dependency);
	}

	if (!PROGRESS_BLOCKED (task)) {
		if (FLAG_IS_ON (task, IRIS_TASK_FLAG_NEED_EXECUTE))
			iris_task_schedule (task);
		else if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE))
			iris_task_progress_callbacks_or_finish (task);
	}
}

/**************************************************************************
 *                    IrisTask Message Handling Methods                   *
 *************************************************************************/

static void
handle_start_work (IrisTask    *task,
                   IrisMessage *message)
{
	IrisTaskPrivate *priv;
	const GValue    *value = NULL;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_WORK_ACTIVE));

	ENABLE_FLAG (task, IRIS_TASK_FLAG_STARTED);
	g_object_ref_sink (task);

	priv = task->priv;
	value = iris_message_get_data (message);

	if (G_VALUE_TYPE (value) == G_TYPE_OBJECT)
		priv->async_result = g_value_get_object (value);

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED))
		/* Don't run if we have already been cancelled. We should probably
		 * do something with the async result here.
		 */
		return;

	ENABLE_FLAG (task, IRIS_TASK_FLAG_NEED_EXECUTE);

	if (!PROGRESS_BLOCKED (task))
		iris_task_schedule (task);
}

static void
handle_progress_callbacks (IrisTask    *task,
                           IrisMessage *message)
{
	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	g_return_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_WORK_ACTIVE) ||
	                  FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE));
	g_return_if_fail (task->priv->dependencies == NULL);

	iris_task_progress_callbacks_or_finish (task);
}

static void
handle_work_finished (IrisTask    *task,
                      IrisMessage *message)
{
	g_return_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_WORK_ACTIVE));
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE));
	g_return_if_fail (task->priv->dependencies == NULL);

	DISABLE_FLAG (task, IRIS_TASK_FLAG_WORK_ACTIVE);
	ENABLE_FLAG (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE);

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED)) {
		/* Cancel happened after work function completed. So let's ignore
		 * it. */
		DISABLE_FLAG (task, IRIS_TASK_FLAG_CANCELLED);
	}

	if (!PROGRESS_BLOCKED (task))
		iris_task_progress_callbacks (task);
}

static void
handle_callbacks_finished (IrisTask    *task,
                           IrisMessage *message)
{
	IrisTaskPrivate *priv;
	IrisMessage     *finish_message;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	g_return_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_WORK_ACTIVE) ||
	                  FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE));

	priv = task->priv;

	/* Callbacks should all have executed and been removed from the list */
	g_return_if_fail (priv->handlers == NULL);

	ENABLE_FLAG (task, IRIS_TASK_FLAG_FINISHED);
	DISABLE_FLAG (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE);

	iris_task_notify_observers (task);

	finish_message = iris_message_new (IRIS_TASK_MESSAGE_FINISH);
	iris_port_post (task->priv->port, finish_message);
}

static void
handle_start_cancel (IrisTask    *task,
                     IrisMessage *start_message)
{
	IrisTaskPrivate *priv;
	IrisMessage     *finish_message;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (start_message != NULL);

	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_CANCELLED));

	priv = task->priv;

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CALLBACKS_ACTIVE) ||
	    FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED))
		/* Too late to cancel */
		return;

	if (IRIS_TASK_GET_CLASS (task)->can_cancel (task)) {
		DISABLE_FLAG (task, IRIS_TASK_FLAG_NEED_EXECUTE);

		ENABLE_FLAG (task, IRIS_TASK_FLAG_CANCELLED);

		iris_task_notify_observers (task);

		if (FLAG_IS_ON (task, IRIS_TASK_FLAG_WORK_ACTIVE));
			/* FINISH_CANCEL message will be sent when work function exits. */
		else {
			/* Work function already finished/hasn't started/finished between
			 * the last two instructions and already sent this message (but we
			 * check for double emission).
			 */
			finish_message = iris_message_new (IRIS_TASK_MESSAGE_FINISH_CANCEL);
			iris_port_post (priv->port, finish_message);
		}
	}
}

static void
handle_finish_cancel (IrisTask    *task,
                      IrisMessage *message)
{
	IrisMessage *finish_message;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	/* Check for double emission, which is allowed */
	if (! g_atomic_int_compare_and_exchange (&task->priv->cancel_finished, FALSE, TRUE))
		return;

	ENABLE_FLAG (task, IRIS_TASK_FLAG_FINISHED);

	finish_message = iris_message_new (IRIS_TASK_MESSAGE_FINISH);
	iris_port_post (task->priv->port, finish_message);
}

static void
handle_finish (IrisTask    *task,
               IrisMessage *message)
{
	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	/* This would be a good thing to do, but we need to be clever because
	 * iris_task_run() and iris_process_enqueue() etc. are allowed even after a
	 * cancel ...
	 */
	#if 0
	/* FIXME: this test should be a function like iris_port_is_clear() ... */
	if (iris_port_get_queue_length (task->priv->port) > 0 ||
	    g_atomic_int_get (&task->priv->receiver->priv->active) > 0) {
		if (iris_task_is_cancelled (task))
			g_warning ("Task %lx is entering finished state due to cancel, but "
			           "there are more messages waiting to execute. Don't "
			           "forget that IrisTasks are immutable once they have "
			           "started running or been cancelled.\n", (gulong)task);
		else
			g_warning ("Task %lx is entering finished state, but there are "
			           "more messages waiting to execute. Don't forget that "
			           "IrisTasks are immutable once they have started running or "
			           "been cancelled.\n", (gulong)task);
		return;
	}
	#endif

	iris_task_complete_async_result (task);

	/* FIXME: this ref is added even when the task is a process with a sink
	 * process that will have taken this ref ...
	 */
	if (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED))
		g_object_ref_sink (task);

	/* The actual unref happens in iris_task_handle_message() because it
	 * still needs to access priv->in_message_handler
	 */
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
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;
	dep = g_value_get_object (iris_message_get_data (message));

	priv->dependencies = g_list_prepend (priv->dependencies,
	                                     g_object_ref (dep));

	msg = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_OBSERVER,
	                             IRIS_TYPE_TASK, task);
	iris_port_post (dep->priv->port, msg);
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

static void
handle_add_handler (IrisTask    *task,
                    IrisMessage *message)
{
	IrisTaskPrivate *priv;
	IrisTaskHandler *handler;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

	priv = task->priv;

	if (!(handler = g_value_get_pointer (iris_message_get_data (message))))
		return;

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_STARTED)) {
		g_warning ("IrisTask: callbacks cannot be added to a task once "
		           "iris_task_run() has been called.");
		return;
	}

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED))
		return;

	priv->handlers = g_list_append (priv->handlers, handler);
}

static void
handle_set_main_context (IrisTask    *task,
                         IrisMessage *message)
{
	IrisTaskPrivate *priv;
	GMainContext    *context;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);
	g_return_if_fail (FLAG_IS_OFF (task, IRIS_TASK_FLAG_STARTED));

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

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED))
		return;

	task_class->dependency_finished (task, dep);

	if (!PROGRESS_BLOCKED (task)) {
		if (FLAG_IS_ON (task, IRIS_TASK_FLAG_NEED_EXECUTE))
			iris_task_schedule (task);
		else if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED))
			iris_task_progress_callbacks_or_finish (task);
	}
}

static void
handle_dep_cancelled (IrisTask    *task,
                     IrisMessage *message)
{
	IrisTaskClass *task_class;
	IrisTask      *dep;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	task_class = IRIS_TASK_GET_CLASS (task);
	dep = IRIS_TASK (g_value_get_object (iris_message_get_data (message)));

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED) ||
	    FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED))
		return;

	task_class->dependency_cancelled (task, dep);
}

static void
handle_add_observer (IrisTask    *task,
                     IrisMessage *in_message)
{
	IrisTaskPrivate *priv;
	IrisTask        *observer;
	IrisMessage     *dep_message;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (in_message != NULL);

	priv = task->priv;
	observer = g_value_get_object (iris_message_get_data (in_message));

	dep_message = NULL;
	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED))
		dep_message = iris_message_new_data (IRIS_TASK_MESSAGE_DEP_CANCELLED,
		                                     IRIS_TYPE_TASK, task);
	else
	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED))
		dep_message = iris_message_new_data (IRIS_TASK_MESSAGE_DEP_FINISHED,
		                                     IRIS_TYPE_TASK, task);

	if (dep_message != NULL) {
		/* If you move where iris_task_notify_observers() gets called to earlier,
		 * make sure there's no possibility of a double notify/no notify at all.
		 */
		iris_port_post (observer->priv->port, dep_message);
		return;
	}

	/* We don't ref observers since they are dependent on us, and so will be
	 * cancelled if we cancel, can't complete until we complete, etc.
	 */
	priv->observers = g_list_prepend (priv->observers,
	                                  observer);
}

static void
handle_remove_observer (IrisTask    *task,
                        IrisMessage *message)
{
	IrisTaskPrivate *priv;
	IrisTask        *observer;
	GList           *node;

	g_return_if_fail (IRIS_IS_TASK (task));
	g_return_if_fail (message != NULL);

	priv = task->priv;

	observer = g_value_get_object (iris_message_get_data (message));

	node = g_list_find (priv->observers, observer);
	if (node != NULL)
		priv->observers = g_list_delete_link (priv->observers, node);
	else {
		/* It's valid for the observer to not have registered, but only if we
		 * were already cancelled/completed so we just sent the
		 * dep-cancelled/finished message directly
		 */
		g_warn_if_fail (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED) ||
		                FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED));
	}
}

#ifdef IRIS_TRACE_TASK
const char *task_message_name[] =
  { "start-work",
    "work-finished",
    "progress-callbacks",
    "callbacks-finished",
    "start-cancel",
    "finish-cancel",
    "finish",
    "add-handler",
    "add-dependency",
    "remove-dependency",
    "set-main-context",
    "dep-finished",
    "dep-cancelled",
    "add-observer",
    "remove-observer"
  };
#endif

static void
iris_task_handle_message_real (IrisTask    *task,
                               IrisMessage *message)
{
	IrisTaskPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (task));

	priv = task->priv;

	#ifdef IRIS_TRACE_TASK
	if (message->what >= IRIS_TASK_MESSAGE_START_WORK &&
	    message->what <= IRIS_TASK_MESSAGE_REMOVE_OBSERVER)
		g_print ("task %lx: got message %s\n",
		         (gulong)task, task_message_name [message->what-1]);
	#endif

	switch (message->what) {
	case IRIS_TASK_MESSAGE_START_WORK:
		handle_start_work (task, message);
		break;
	case IRIS_TASK_MESSAGE_PROGRESS_CALLBACKS:
		handle_progress_callbacks (task,message);
		break;
	case IRIS_TASK_MESSAGE_WORK_FINISHED:
		handle_work_finished (task, message);
		break;
	case IRIS_TASK_MESSAGE_CALLBACKS_FINISHED:
		handle_callbacks_finished (task, message);
		break;
	case IRIS_TASK_MESSAGE_START_CANCEL:
		handle_start_cancel (task, message);
		break;
	case IRIS_TASK_MESSAGE_FINISH_CANCEL:
		handle_finish_cancel (task, message);
		break;
	case IRIS_TASK_MESSAGE_FINISH:
		handle_finish (task, message);
		break;
	case IRIS_TASK_MESSAGE_ADD_HANDLER:
		handle_add_handler (task, message);
		break;
	case IRIS_TASK_MESSAGE_ADD_DEPENDENCY:
		handle_add_dependency (task, message);
		break;
	case IRIS_TASK_MESSAGE_REMOVE_DEPENDENCY:
		handle_remove_dependency (task, message);
		break;
	case IRIS_TASK_MESSAGE_SET_MAIN_CONTEXT:
		handle_set_main_context (task, message);
		break;
	case IRIS_TASK_MESSAGE_DEP_FINISHED:
		handle_dep_finished (task, message);
		break;
	case IRIS_TASK_MESSAGE_DEP_CANCELLED:
		handle_dep_cancelled (task, message);
		break;
	case IRIS_TASK_MESSAGE_ADD_OBSERVER:
		handle_add_observer (task, message);
		break;
	case IRIS_TASK_MESSAGE_REMOVE_OBSERVER:
		handle_remove_observer (task, message);
		break;
	default:
		g_warn_if_reached ();
		break;
	}
}

/**************************************************************************
 *                 IrisTask Class VTable Implementations                  *
 *************************************************************************/

static void
iris_task_dependency_cancelled_real (IrisTask *task,
                                    IrisTask *dependency)
{
	iris_task_cancel (task);
	iris_task_remove_dependency (task, dependency);
}

static void
iris_task_dependency_finished_real (IrisTask *task,
                                    IrisTask *dependency)
{
	iris_task_remove_dependency (task, dependency);
}

static void
task_was_finalized_cb (gpointer user_data,
                       GObject *where_the_object_was)
{
	gboolean *p_task_was_finalized = user_data;
	*p_task_was_finalized = TRUE;
}

static void
iris_task_handle_message (IrisMessage *message,
                          gpointer     data)
{
	IrisTask *task;
	gboolean  task_was_finalized = FALSE;

	g_return_if_fail (IRIS_IS_TASK (data));

	task = IRIS_TASK (data);

	/* We have some major control message sync. problems if this happens! */
	g_return_if_fail (task->priv->in_message_handler == FALSE);

	task->priv->in_message_handler = TRUE;

	IRIS_TASK_GET_CLASS (task)->handle_message (task, message);

	if (message->what == IRIS_TASK_MESSAGE_FINISH) {
		/* We are tasked with removing the execution reference. It's slightly
		 * awkward because the task could still have other references, and
		 * although they shouldn't send any more messages let's try and be
		 * robust. An if (ref_count == 1) check could race with g_object_ref()
		 * so we use a weak-ref to check if the object was finalized.
		 */
		g_object_weak_ref (G_OBJECT (task),
		                   task_was_finalized_cb,
		                   &task_was_finalized);
		g_object_unref (task);

		#ifdef IRIS_TRACE_TASK
		if (task_was_finalized)
			g_print ("task %lx: freed after finish\n", (gulong)task);
		else
			g_print ("task %lx: not freed, refs now %i\n", (gulong)task,
			         G_OBJECT(task)->ref_count);
		#endif

		if (! task_was_finalized) {
			task->priv->in_message_handler = FALSE;
			g_object_weak_unref (G_OBJECT (task),
			                     task_was_finalized_cb,
			                     &task_was_finalized);
		}
	} else {
		task->priv->in_message_handler = FALSE;
	}
}

static gboolean
iris_task_can_cancel_real (IrisTask *task)
{
	return TRUE;
}

static void
iris_task_execute_real (IrisTask *task)
{
	GValue       params = {0,};
	IrisMessage *message;

	g_value_init (&params, G_TYPE_OBJECT);
	g_value_set_object (&params, task);
	g_closure_invoke (task->priv->closure, NULL, 1, &params, NULL);
	g_value_unset (&params);

	if (FLAG_IS_ON (task, IRIS_TASK_FLAG_CANCELLED)) {
		/* Cancel will have been deferred until work function notices */
		message = iris_message_new (IRIS_TASK_MESSAGE_FINISH_CANCEL);
		iris_port_post (task->priv->port, message);
	} else
	if (FLAG_IS_OFF (task, IRIS_TASK_FLAG_ASYNC))
		iris_task_work_finished (task);
}

static gboolean
iris_task_has_succeeded_real (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	priv = task->priv;

	return (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED) &&
	        FLAG_IS_OFF (task, IRIS_TASK_FLAG_CANCELLED) &&
	        (priv->error == NULL));
}

static gboolean
iris_task_has_failed_real (IrisTask *task)
{
	IrisTaskPrivate *priv;

	g_return_val_if_fail (IRIS_IS_TASK (task), FALSE);

	priv = task->priv;

	return (FLAG_IS_ON (task, IRIS_TASK_FLAG_FINISHED) &&
	        FLAG_IS_OFF (task, IRIS_TASK_FLAG_CANCELLED) &&
	        priv->error != NULL);
}

static void
iris_task_constructed (GObject *object)
{
	IrisTask        *task;
	IrisTaskPrivate *priv;

	/* Chaining up to GObject was broken until GObject 2.27.93 */
	/*G_OBJECT_CLASS (iris_task_parent_class)->constructed (object); */

	task = IRIS_TASK (object);
	priv = task->priv;

	/* These are construct-only properties and will have been set to the default
	 * value by set_property() by now if they were not explicity set
	 */
	g_warn_if_fail (priv->control_scheduler != NULL);
	g_warn_if_fail (priv->work_scheduler != NULL);

	priv->port = iris_port_new ();
	priv->receiver = iris_arbiter_receive (priv->control_scheduler,
	                                       priv->port,
	                                       iris_task_handle_message,
	                                       task,
	                                       NULL);

	iris_arbiter_coordinate (priv->receiver, NULL, NULL);
}

static void
iris_task_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	IrisTask        *task;
	IrisTaskPrivate *priv;
	IrisScheduler   *scheduler;

	task = IRIS_TASK (object);
	priv = task->priv;

	switch (prop_id) {
		/* Construct-only properties */
		case PROP_CONTROL_SCHEDULER:
			g_warn_if_fail (priv->control_scheduler == NULL);

			scheduler = g_value_get_object (value);
			if (scheduler == NULL)
				scheduler = iris_get_default_control_scheduler ();

			priv->control_scheduler = g_object_ref (scheduler);
			break;

		case PROP_WORK_SCHEDULER:
			g_warn_if_fail (priv->work_scheduler == NULL);

			scheduler = g_value_get_object (value);
			if (scheduler == NULL)
				scheduler = iris_get_default_work_scheduler ();

			priv->work_scheduler = g_object_ref (scheduler);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
iris_task_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	IrisTask        *task;
	IrisTaskPrivate *priv;

	task = IRIS_TASK (object);
	priv = task->priv;

	switch (prop_id) {
		/* Construct-only properties */
		case PROP_CONTROL_SCHEDULER:
			g_value_set_object (value, priv->control_scheduler);
			break;
		case PROP_WORK_SCHEDULER:
			g_value_set_object (value, priv->work_scheduler);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
iris_task_finalize (GObject *object)
{
	IrisTaskPrivate *priv;

	priv = IRIS_TASK(object)->priv;

	g_object_unref (priv->control_scheduler);
	g_object_unref (priv->work_scheduler);

	iris_receiver_destroy (priv->receiver, priv->in_message_handler);
	g_object_unref (priv->port);

	g_mutex_free (priv->mutex);

	if (G_VALUE_TYPE (&priv->result) != G_TYPE_INVALID)
		g_value_unset (&priv->result);

	if (priv->error) {
		g_error_free (priv->error);
		priv->error = NULL;
	}

	if (priv->closure != NULL) {
		g_closure_invalidate (priv->closure);
		g_closure_unref (priv->closure);
		priv->closure = NULL;
	}

	if (priv->dependencies != NULL) {
		/* Free deps (if we were cancelled, otherwise they must be completed) */
		g_list_foreach (priv->dependencies, (GFunc)g_object_unref, NULL);
		g_list_free (priv->dependencies);
		priv->dependencies = NULL;
	}

	G_OBJECT_CLASS (iris_task_parent_class)->finalize (object);
}

static void
iris_task_class_init (IrisTaskClass *task_class)
{
	GObjectClass *object_class;

	task_class->handle_message = iris_task_handle_message_real;
	task_class->can_cancel = iris_task_can_cancel_real;
	task_class->execute = iris_task_execute_real;
	task_class->has_succeeded = iris_task_has_succeeded_real;
	task_class->has_failed = iris_task_has_failed_real;
	task_class->dependency_finished = iris_task_dependency_finished_real;
	task_class->dependency_cancelled = iris_task_dependency_cancelled_real;

	object_class = G_OBJECT_CLASS (task_class);
	object_class->constructed = iris_task_constructed;
	object_class->set_property = iris_task_set_property;
	object_class->get_property = iris_task_get_property;
	object_class->finalize = iris_task_finalize;

	/**
	 * IrisTask:control-scheduler:
	 *
	 * The #IrisScheduler used for internal control messages.
	 */
	g_object_class_install_property
	  (object_class,
	   PROP_CONTROL_SCHEDULER,
	   g_param_spec_object ("control-scheduler",
	                        "Control Scheduler",
	                        "Scheduler used to process control messages",
	                        IRIS_TYPE_SCHEDULER,
	                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME |
	                        G_PARAM_READWRITE));

	/**
	 * IrisTask:work-scheduler:
	 *
	 * The #IrisScheduler used for executing the task's work function.
	 */
	g_object_class_install_property
	  (object_class,
	   PROP_WORK_SCHEDULER,
	   g_param_spec_object ("work-scheduler",
	                        "Work Scheduler",
	                        "Scheduler used to run task's work function",
	                        IRIS_TYPE_SCHEDULER,
	                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME |
	                        G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof(IrisTaskPrivate));
}

static void
iris_task_init (IrisTask *task)
{
	IrisTaskPrivate *priv;
	GClosure        *closure;

	priv = task->priv = IRIS_TASK_GET_PRIVATE (task);

	priv->control_scheduler = NULL;
	priv->work_scheduler = NULL;

	priv->progress_mode = IRIS_PROGRESS_ACTIVITY_ONLY;

	priv->mutex = g_mutex_new ();

	priv->error = NULL;

	closure = g_cclosure_new (G_CALLBACK (iris_task_dummy), NULL, NULL);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	task->priv->closure = closure;

	priv->handlers = NULL;

	priv->dependencies = NULL;
	priv->observers = NULL;

	priv->flags = 0;
	priv->cancel_finished = FALSE;
	priv->in_message_handler = FALSE;

	priv->context = NULL;
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
