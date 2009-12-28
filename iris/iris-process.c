/* iris-process.c
 *
 * Copyright (C) 2009 Sam Thursfield <ssssam@gmail.com>
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

#include "iris-receiver-private.h"
#include "iris-task-private.h"
#include "iris-process.h"
#include "iris-process-private.h"

#include <stdio.h>

/**
 * SECTION:iris-process
 * @title: IrisProcess
 * @short_description: A concurrent and asynchronous process abstraction
 *
 * #IrisProcess is a work queue that operates on atomic work items. An example
 * of this is be reading information from every file in a directory, or
 * processing a complex calculation with a series of inputs. This is a special
 * case of #IrisTask, which is better if you want to do one long task such as
 * downloading a single file.
 *
 * Once the #IrisProcess is created, you can give it work to do with
 * iris_process_enqueue(). You can start it processing the work at any point
 * with iris_process_run(). When you have finished enqueuing work you MUST call
 * iris_process_no_more_work(), so the thread will exit once it has finished
 * working.
 *
 * The order the work items are executed in is unspecified. If they depend on
 * each other, you will be better served by #IrisTask, or by breaking the
 * function up into a series of chained processes.
 *
 * The progress of an #IrisProcess can be monitored using some kind of
 * #IrisProgressMonitor, such as an #IrisProgressDialog. If you want to do this
 * you may want to call iris_process_set_title() to give the process a label.
 */

/* How frequently the process checks for cancellation between try_pop calls. */
#define WAKE_UP_INTERVAL  20000

enum {
	SOURCE_CONNECTED,
	SINK_CONNECTED,
	LAST_SIGNAL
};

#define FLAG_IS_ON(p,f)  ((IRIS_TASK(p)->priv->flags & f) != 0)
#define FLAG_IS_OFF(p,f) ((IRIS_TASK(p)->priv->flags & f) == 0)
#define ENABLE_FLAG(p,f) G_STMT_START{IRIS_TASK(p)->priv->flags|=f;}G_STMT_END
#define DISABLE_FLAG(p,f) G_STMT_START{IRIS_TASK(p)->priv->flags&=~f;}G_STMT_END

G_DEFINE_TYPE (IrisProcess, iris_process, IRIS_TYPE_TASK);

static void             iris_process_dummy         (IrisProcess *task,
                                                    IrisMessage *work_item,
                                                    gpointer user_data);


/* Used to pass the parameters for a source/sink-connected signal to an idle
 * callback, process_connected_idle_callback(). */
typedef struct {
	IrisProcess *process;
	IrisProcess *connected_process;
	int          signal_id;
} IrisProcessConnectionClosure;

/* Used to pass the process state to an idle callback,
 * progress_watch_idle_callback(), which in turn calls an IrisProgressMonitor
 * object's watch callback. */
typedef struct {
	IrisTask *task;
	gint      completed, total;
	gboolean  cancelled;
	GClosure *watch_callback;
} IrisProcessWatchClosure;

static guint process_signals[LAST_SIGNAL] = { 0 };

/* FIXME: iris-marshall.h, and generate automatically .. */
static void
g_cclosure_marshal_VOID__INT_INT_BOOLEAN (GClosure *closure,
                                          GValue *return_value,
                                          guint n_param_values,
                                          const GValue *param_values,
                                          gpointer invocation_hint,
                                          gpointer marshal_data)
{
	typedef void (*GMarshalFunc_VOID__INT_INT_BOOLEAN) (gpointer data1,
	                                                    gpointer arg_1,
	                                                    gpointer arg_2,
	                                                    gpointer arg_3,
	                                                    gpointer data2);
	register GMarshalFunc_VOID__INT_INT_BOOLEAN callback;
	register GCClosure *cc = (GCClosure*) closure;
	register gpointer   data1, data2;

	g_return_if_fail (n_param_values==4);

	if (G_CCLOSURE_SWAP_DATA (closure)) {
		data1=closure->data;
		data2=g_value_peek_pointer (param_values+0);
	} else {
		data1 = g_value_peek_pointer (param_values+0);
		data2 = closure->data;
	}

	callback = (GMarshalFunc_VOID__INT_INT_BOOLEAN) (marshal_data ?
	                                                 marshal_data :
	                                                 cc->callback);
	callback (data1,
	          GINT_TO_POINTER(g_value_get_int(param_values+1)),
	          GINT_TO_POINTER(g_value_get_int(param_values+2)),
	          GINT_TO_POINTER(g_value_get_boolean(param_values+3)),
	          data2);
}

/**************************************************************************
 *                          IrisProcess Public API                       *
 *************************************************************************/

/**
 * iris_process_new:
 *
 * Creates a new instance of #IrisProcess.
 *
 * Return value: the newly created instance of #IrisProcess
 */
IrisProcess*
iris_process_new (void)
{
	return g_object_new (IRIS_TYPE_PROCESS, NULL);
}

/**
 * iris_process_new_with_func:
 * @func: An #IrisProcessFunc to call for each work item.
 * @user_data: user data for @func
 * @notify: An optional #GDestroyNotify or %NULL
 *
 * Create a new #IrisProcess instance.
 *
 * Return value: the newly created #IrisTask instance
 */
IrisProcess*
iris_process_new_with_func (IrisProcessFunc   func,
                            gpointer          user_data,
                            GDestroyNotify    notify)
{
	IrisProcess *process;

	process = g_object_new (IRIS_TYPE_PROCESS, NULL);

	iris_process_set_func (process, func, user_data, notify);

	return process;
}

/**
 * iris_process_new_with_closure:
 * @closure: A #GClosure
 *
 * Creates a new process using the closure for execution.
 *
 * Return value: the newly created #IrisProcess
 */
IrisProcess*
iris_process_new_with_closure (GClosure *closure)
{
	IrisProcess *process;

	g_return_val_if_fail (closure != NULL, NULL);

	process = g_object_new (IRIS_TYPE_PROCESS, NULL);

	iris_process_set_closure (process, closure);

	return process;
}

/**
 * iris_process_run:
 * @process: An #IrisProcess
 *
 * Starts the #IrisProcess executing work items. Its thread will run until it
 * runs out of work AND iris_process_no_more_data() (or iris_process_cancel())
 * is called.
 *
 * If @process has any successors, they will also start. For more information,
 * see iris_process_connect().
 */
void
iris_process_run (IrisProcess *process)
{
	g_return_if_fail (IRIS_IS_PROCESS (process));

	iris_task_run (IRIS_TASK (process));

	/* Starting successors occurs in the execute handler. */
}

/**
 * iris_process_cancel:
 * @process: An #IrisProcess
 *
 * Cancels a process.  The process will exit after a work item is complete, and
 * the task function can also periodically check the cancelled state with
 * iris_task_is_canceled() and quit execution.
 *
 * If @process has any predecessors, they will also be cancelled. For more
 * information, see iris_process_connect().
 */
void
iris_process_cancel (IrisProcess *process)
{
	iris_task_cancel (IRIS_TASK (process));
}

/**
 * iris_process_enqueue:
 * @process: An #IrisProcess
 * @work_item: An #IrisMessage
 *
 * Posts a work item to the queue. The task function will be passed @work_item
 * when the work item is executed. The type and contents of the message are
 * entirely up to you.
 *
 * The work items should not depend in any way on order of execution - if this
 * is a problem, you should create individual #IrisTask objects which allow you
 * to specify dependencies.
 */
void
iris_process_enqueue (IrisProcess *process,
                      IrisMessage *work_item)
{
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	if (FLAG_IS_ON(process, IRIS_PROCESS_FLAG_NO_MORE_WORK)) {
		g_warning ("iris_process_enqueue: cannot enqueue more work items, "
		           "because iris_process_no_more_work() has been called on "
		           "this process.");
		return;
	};

	g_atomic_int_inc (&priv->total_items);

	iris_port_post (priv->work_port, work_item);
	iris_message_unref (work_item);
};

/**
 * iris_process_no_more_work:
 * @process: An #IrisProcess
 *
 * Causes @process to finish up the work it is doing and then destroy itself.
 * If this function is never called, @process will stay around idling and using
 * a substantial amount of CPU cycles forever.
 *
 * Any processes chained to the output of @process will also stop once they run
 * out of work.
 */
void
iris_process_no_more_work (IrisProcess *process)
{
	IrisTaskPrivate *task_priv = IRIS_TASK(process)->priv;

	IrisMessage *message = iris_message_new (IRIS_PROCESS_MESSAGE_NO_MORE_WORK);
	iris_port_post (task_priv->port, message);
	iris_message_unref (message);
};

/**
 * iris_process_connect:
 * @head: An #IrisProcess
 * @tail: An #IrisProcess
 *
 * Connects @head to @tail, so @tail receives its events from @head when it is
 * done processing them. @head and @tail must not yet be running.
 *
 * The work function of @head needs to do something like this to propagate work
 * to @tail:
 * |[
 *   if (iris_process_has_successor (self))
 *     {
 *       IrisMessage *work_item = iris_message_new (1);
 *       iris_message_set_pointer (pointer_to_some_work);
 *       iris_process_forward (self, work_item);
 *     }
 * ]|
 *
 * When processes are chained together, the behaviour of other functions is
 * altered:
 * <itemizedlist>
 *  <listitem>
 *   <para>
 *     run events are propagated DOWN the chain. To start work, you need to
 *     call:
 *       |[ iris_process_run (head); ]|
 *     And the whole chain will start processing.
 *   </para>
 *  </listitem>
 *  <listitem>
 *   <para>
 *     iris_process_no_more_work() only needs to be called @head.
 *     iris_process_is_finished(tail) will return %TRUE when @tail has no work
 *     queued and iris_process_no_more_work() has been called for @head (or for
 *     the first process in the chain, if @head in turn has a source process).
 *   </para>
 *  </listitem>
 *  <listitem>
 *   <para>
 *     cancel events are propagated UP the chain, so to cancel a whole chain
 *     you should call:
 *       |[ iris_process_cancel (tail); ]|
 *     If you cancel @head, @tail will carry on processing work.
 *   </para>
 *  </listitem>
 * </itemizedlist>
 *
 * Callbacks and errbacks can still be connected to any process, using
 * iris_task_add_callback() and iris_task_add_errback(), but for them to be
 * called when the entire chain ends you must add them to @tail.
 *
 * It is not currently possible to disconnect processes. A process can only have
 * one source and one sink process. Both these things shouldn't be too hard to
 * implement.
 */
void
iris_process_connect (IrisProcess *head,
                      IrisProcess *tail)
{
	IrisTaskPrivate *head_task_priv,
	                *tail_task_priv;

	IrisMessage *head_message,
	            *tail_message;

	g_return_if_fail (IRIS_IS_PROCESS (head));
	g_return_if_fail (IRIS_IS_PROCESS (tail));

	head_task_priv = IRIS_TASK (head)->priv;
	tail_task_priv = IRIS_TASK (tail)->priv;

	if (FLAG_IS_ON (head, IRIS_TASK_FLAG_EXECUTING) ||
	    FLAG_IS_ON (tail, IRIS_TASK_FLAG_EXECUTING)) {
		g_warning ("iris_process_connect: %s process is already running.\n"
		           "You can only chain processes together when the head "
		           "process is not yet executing.",
		           FLAG_IS_ON (head, IRIS_TASK_FLAG_EXECUTING) ? "head"
		                                                       : "tail");
		return;
	}

	if (FLAG_IS_ON (head, IRIS_PROCESS_FLAG_HAS_SINK)) {
		g_warning ("iris_process_connect: head process already has a sink process.");
		return;
	}

	if (FLAG_IS_ON (tail, IRIS_PROCESS_FLAG_HAS_SOURCE)) {
		g_warning ("iris_process_connect: tail process already has a source process.");
		return;
	}

	/* We send separate messages, but because we require connecting to be done
	 * before the processes are executing, there is no danger of entering an
	 * inconsistent state.
	 */
	head_message = iris_message_new_data (IRIS_PROCESS_MESSAGE_ADD_SINK,
	                                      IRIS_TYPE_PROCESS, tail);
	iris_port_post (head_task_priv->port, head_message);
	iris_message_unref (head_message);

	tail_message = iris_message_new_data (IRIS_PROCESS_MESSAGE_ADD_SOURCE,
	                                      IRIS_TYPE_PROCESS, head);
	iris_port_post (tail_task_priv->port, tail_message);
	iris_message_unref (tail_message);
}

/**
 * iris_process_forward:
 * @process: An #IrisProcess
 * @work_item: An #IrisMessage
 *
 * This function is for use during the process' work function. When called, it
 * forwards @work_item to the successor of @process. @process and its successor
 * must have been connected using iris_process_connect().
 *
 * @work_item should be a newly-created #IrisMessage, and not the same one
 * passed to the current work function.
 */
void
iris_process_forward (IrisProcess *process,
                      IrisMessage *work_item)
{
	IrisProcessPrivate *priv;
	IrisProcess        *sink;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_HAS_SINK));

	priv = process->priv;

	sink = g_atomic_pointer_get (&priv->sink);
	iris_process_enqueue (sink, work_item);
}

/**
 * iris_process_recurse:
 * @process: An #IrisProcess
 * @work_item: An #IrisMessage
 *
 * This function is for use during the process' work function. When called, it
 * enqueues @work_item in @process. You might want to do this if you have a
 * process which operates on directories and their subdirectories recursively,
 * for example.
 *
 * In this situation, iris_process_enqueue() would fail if you had called
 * iris_process_no_more_work() already. Using iris_process_recurse() means you
 * can enqueue an initial set of data and then call iris_process_no_more_work(),
 * and iris_process_is_finished() will still function correctly.
 *
 * @work_item should be a newly-created #IrisMessage, and not the same one
 * passed to the current work function.
 */
void
iris_process_recurse (IrisProcess *process,
                      IrisMessage *work_item)
{
	/* This function behaves the same as iris_process_enqueue but without
	 * checking for IRIS_PROCESS_FLAG_NO_MORE_WORK. It's useful having it as a
	 * separate function for safety.
	 */
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	g_atomic_int_inc (&priv->total_items);

	iris_port_post (priv->work_port, work_item);
	iris_message_unref (work_item);
}

/**
 * iris_process_is_canceled:
 * @process: An #IrisProcess
 *
 * Checks if a process has been cancelled.  Note that if the process handles
 * the cancel and chooses to ignore it, %FALSE will be returned.
 *
 * Return value: %TRUE if the process was canceled.
 */
gboolean
iris_process_is_canceled (IrisProcess *process)
{
	return iris_task_is_canceled (IRIS_TASK (process));
}

/**
 * iris_process_is_finished:
 * @process: An #IrisProcess
 *
 * Checks to see if the process has finished all of its work, and had
 * iris_process_no_more_work() called on it.
 *
 * If @process is chained to a source process, it has finished when it has
 * completed all of its work and the first process in the chain has finished.
 *
 * Return value: %TRUE if the process has completed
 */
gboolean
iris_process_is_finished (IrisProcess *process)
{
	IrisProcessPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROCESS (process), FALSE);

	priv = process->priv;

	if (iris_process_has_predecessor (process)) {
		if (iris_process_get_queue_length (process) > 0 &&
		    FLAG_IS_OFF (process, IRIS_TASK_FLAG_CANCELED))
		    return FALSE;
		    
		return iris_process_is_finished (priv->source);
	}
	else
		return iris_task_is_finished (IRIS_TASK (process));
};

/**
 * iris_process_has_predecessor:
 * @process: An #IrisProcess
 *
 * Checks if @process has another process feeding it work. For more
 * information, see iris_process_connect().
 *
 * Return value: %TRUE if the process has a predecessor connected.
 */
gboolean
iris_process_has_predecessor (IrisProcess *process)
{
	g_return_val_if_fail (IRIS_IS_PROCESS (process), FALSE);

	return FLAG_IS_ON (process, IRIS_PROCESS_FLAG_HAS_SOURCE);
}

/**
 * iris_process_has_successor:
 * @process: An #IrisProcess
 *
 * Checks if @process is connected to a process that it can send work to using
 * iris_process_forward(). For more information, see iris_process_connect().
 *
 * Return value: %TRUE if the process has a successor connected.
 */
gboolean
iris_process_has_successor (IrisProcess *process)
{
	g_return_val_if_fail (IRIS_IS_PROCESS (process), FALSE);

	return FLAG_IS_ON (process, IRIS_PROCESS_FLAG_HAS_SINK);
}

/**
 * iris_process_get_queue_length:
 * @process: An #IrisProcess
 *
 * Returns the number of work items enqueued in @process but not yet
 * executed.
 *
 * Return value: how many items @process has left to complete.
 */
gint
iris_process_get_queue_length (IrisProcess *process) {
	IrisProcessPrivate *priv;
	gint               processed_items, total_items;

	g_return_val_if_fail (IRIS_IS_PROCESS (process), 0);

	priv = process->priv;

	/* Reading the values in this order means could ignore items that complete
	 * concurrently, and give a queue lengths which is too high.
	 */
	processed_items = g_atomic_int_get (&priv->processed_items);
	total_items = g_atomic_int_get (&priv->total_items);
	return total_items - processed_items;
}

/**
 * iris_process_get_predecessor:
 * @process: An #IrisProcess
 *
 * Returns the previous process in the chain from @process, or %NULL.
 *
 * Return value: a pointer to the an #IrisProcess, or %NULL
 */
IrisProcess*  iris_process_get_predecessor      (IrisProcess            *process)
{
	g_return_val_if_fail (IRIS_IS_PROCESS (process), NULL);

	return process->priv->source;
}

/**
 * iris_process_get_successor:
 * @process: An #IrisProcess
 *
 * Returns the next process in the chain from @process, or %NULL.
 *
 * Return value: a pointer to the an #IrisProcess, or %NULL
 */
IrisProcess*  iris_process_get_successor        (IrisProcess            *process)
{
	g_return_val_if_fail (IRIS_IS_PROCESS (process), NULL);

	return process->priv->sink;
}

/**
 * iris_process_get_title:
 * @process: An #IrisProcess
 *
 * Returns the title of @process, as set by iris_process_set_title()/
 *
 * Return value: a pointer to a string.
 */
const gchar *
iris_process_get_title (IrisProcess  *process)
{
	g_return_val_if_fail (IRIS_IS_PROCESS (process), NULL);

	return process->priv->title;
}

/**
 * iris_process_set_func:
 * @process: An #IrisProcess
 * @func: An #IrisProcessFunc to call for each work item.
 * @user_data: user data for @func
 * @notify: An optional #GDestroyNotify or %NULL
 *
 * Sets the work function of @process. This function cannot be called after
 * iris_process_run().
 */
void
iris_process_set_func (IrisProcess     *process,
                       IrisProcessFunc  func,
                       gpointer         user_data,
                       GDestroyNotify   notify)
{
	GClosure    *closure;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (FLAG_IS_OFF (process, IRIS_TASK_FLAG_EXECUTING));

	if (!func)
		func = iris_process_dummy;

	closure = g_cclosure_new (G_CALLBACK (func),
	                          user_data,
	                          (GClosureNotify)notify);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__POINTER);
	iris_process_set_closure (process, closure);
	g_closure_unref (closure);
}

/**
 * iris_process_set_closure:
 * @process: An #IrisProcess
 * @closure: A #GClosure
 *
 * Sets the work function of @process as @closure. This function cannot be called after
 * iris_process_run().
 */
void
iris_process_set_closure (IrisProcess *process,
                          GClosure    *closure)
{
	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (closure != NULL);

	if (G_LIKELY (IRIS_TASK(process)->priv->closure))
		g_closure_unref (IRIS_TASK(process)->priv->closure);

	IRIS_TASK(process)->priv->closure = g_closure_ref (closure);
}

/**
 * iris_process_set_title:
 * @process: An #IrisProcess
 * @title: A user-visible string saying what @process does.
 *
 * Sets the title of @process. This should be a few words describing what the
 * processes is doing, and will be used in any status dialogs watching the
 * process.
 */
void
iris_process_set_title (IrisProcess *process,
                        const gchar *title)
{
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	g_free (priv->title);

	priv->title = g_strdup (title);
}


/**
 * iris_process_add_watch_callback:
 * @process: An #IrisProcess
 * @callback: An #IrisProgressWatchCallback, called as an idle after each work
 *            item is processed.
 * @user_data: user data for @callback
 * @notify: An optional #GDestroyNotify or %NULL
 *
 * Adds a watch callback to @process. This is useful for creating UI to monitor
 * the progress of the work. @callback will be executed using g_idle_add after
 * each work item is processed, so a GLib main loop must be running for it to be
 * of any use.
 */
void
iris_process_add_watch_callback (IrisProcess               *process,
                                 IrisProgressWatchCallback  callback,
                                 gpointer                   user_data,
                                 GDestroyNotify             notify)
{
	GClosure *closure;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	if (!callback)
		return;

	closure = g_cclosure_new (G_CALLBACK (callback),
	                          user_data,
	                          (GClosureNotify)notify);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__INT_INT_BOOLEAN);
	iris_process_add_watch_closure (process, closure);
	g_closure_unref (closure);
};

/**
 * iris_process_add_watch_closure:
 * @process: An #IrisProcess
 * @closure: A #GClosure, called as an idle after each work item is processed.
 *
 * A variant of iris_process_add_watch_callback() that takes a #GClosure.
 */
void
iris_process_add_watch_closure (IrisProcess *process,
                                GClosure    *closure)
{
	IrisMessage *message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (closure != NULL);

	message = iris_message_new_data (IRIS_PROCESS_MESSAGE_ADD_WATCH_CALLBACK,
	                                 G_TYPE_CLOSURE, closure);
	iris_port_post (IRIS_TASK(process)->priv->port, message);
};

/**************************************************************************
 *                  GMainLoop idle callbacks                              *
 *************************************************************************/

/* These functions are used to ensure callbacks and signal handlers run in the
 * GLib main loop thread, which is important for Gtk+ calls to work. (Note that
 * using gdk_threads_enter()/leave() isn't enough on Windows platforms, it must
 * actually be the main thread or everything breaks).
 */

static gboolean
process_connected_idle_callback (gpointer data) {
	IrisProcessConnectionClosure *state = data;

	g_signal_emit (state->process, state->signal_id, 0,
	               state->connected_process);

	g_slice_free (IrisProcessConnectionClosure, state);

	return FALSE;
}

static gboolean
progress_watch_idle_callback (gpointer data) {
	GValue params[4] = { {0, }, {0, }, {0, } };
	IrisProcessWatchClosure *state = data;

	/* The finished callback has probably got to the monitor before this idle
	 * has been called, so let's not call methods on an object that's been
	 * freed. We do need to notify the watcher this way if we were cancelled,
	 * because in that case it won't receive any callbacks. */
	if (FLAG_IS_ON (state->task, IRIS_TASK_FLAG_FINISHED) &&
	    FLAG_IS_OFF (state->task, IRIS_TASK_FLAG_CANCELED))
		return FALSE;

	g_value_init (&params[0], IRIS_TYPE_TASK);
	g_value_set_object (&params[0], state->task);
	g_value_init (&params[1], G_TYPE_INT);
	g_value_set_int (&params[1], state->completed);
	g_value_init (&params[2], G_TYPE_INT);
	g_value_set_int (&params[2], state->total);
	g_value_init (&params[3], G_TYPE_BOOLEAN);
	g_value_set_boolean (&params[3], state->cancelled);

	g_closure_invoke (state->watch_callback, NULL, 4, params, NULL);

	g_slice_free (IrisProcessWatchClosure, state);

	return FALSE;
}


/**************************************************************************
 *                  IrisProcess Message Handling Methods                  *
 *************************************************************************/

static void
handle_cancel (IrisProcess *process,
               IrisMessage *message)
{
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	/* Cancel the process, and then cancel any predecessors. */
	IRIS_TASK_CLASS (iris_process_parent_class)->handle_message
	  (IRIS_TASK (process), message);

	/* Superclass could have ignored the cancel. */
	if (!iris_process_is_canceled (process))
		return;

	g_return_if_fail (FLAG_IS_ON(process, IRIS_TASK_FLAG_FINISHED));

	if (iris_process_has_predecessor (process)) {
		if (!iris_process_is_finished (priv->source) &&
		    !iris_process_is_canceled (priv->source))
			iris_process_cancel (priv->source);
	}
}

static void
handle_execute (IrisProcess *process,
                IrisMessage *message)
{
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	/* Start the process, and then start any successors. */
	IRIS_TASK_CLASS (iris_process_parent_class)->handle_message
	  (IRIS_TASK (process), message);

	if (iris_process_has_successor (process)) {
		if (!iris_task_is_executing (IRIS_TASK (priv->sink)))
			iris_process_run (priv->sink);
	}
}

static void
handle_no_more_work (IrisProcess *process,
                     IrisMessage *message)
{
	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_NO_MORE_WORK);
}

static void
handle_add_source (IrisProcess *process,
                   IrisMessage *message)
{
	IrisProcessPrivate           *priv;
	const GValue                 *data;
	IrisProcess                  *source_process;
	IrisProcessConnectionClosure *signal_closure;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	data = iris_message_get_data (message);
	source_process = g_value_get_object (data);

	g_return_if_fail (IRIS_IS_PROCESS (source_process));

	priv = process->priv;
	priv->source = source_process;

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_HAS_SOURCE);

	/* Signal. FIXME: it is bad to call g_idle_add if no main loop is running, and the
	 * closure is leaked in that case*/
	signal_closure = g_slice_new (IrisProcessConnectionClosure);
	signal_closure->process = process;
	signal_closure->connected_process = source_process;
	signal_closure->signal_id = process_signals[SOURCE_CONNECTED];
	g_idle_add (process_connected_idle_callback, signal_closure);
}

static void
handle_add_sink (IrisProcess *process,
                 IrisMessage *message)
{
	IrisProcessPrivate           *priv;
	const GValue                 *data;
	IrisProcess                  *sink_process;
	IrisProcessConnectionClosure *signal_closure;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	data = iris_message_get_data (message);
	sink_process = g_value_get_object (data);

	g_return_if_fail (IRIS_IS_PROCESS (sink_process));

	priv = process->priv;
	priv->sink = sink_process;

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_HAS_SINK);

	/* Signal. FIXME: it is bad to call g_idle_add if no main loop is running, and the
	 * closure is leaked */
	signal_closure = g_slice_new (IrisProcessConnectionClosure);
	signal_closure->process = process;
	signal_closure->connected_process = sink_process;
	signal_closure->signal_id = process_signals[SINK_CONNECTED];
	g_idle_add (process_connected_idle_callback, signal_closure);
}

static void
handle_add_watch_callback (IrisProcess *process,
                           IrisMessage *message)
{
	IrisProcessPrivate *priv;
	const GValue       *data;
	GClosure           *closure;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	priv = process->priv;

	data = iris_message_get_data (message);
	closure = g_value_get_boxed (data);

	g_closure_ref (closure);
	priv->watch_callback_list = g_list_append (priv->watch_callback_list,
	                                           closure);
}

static void
iris_process_handle_message_real (IrisTask    *task,
                                  IrisMessage *message)
{
	IrisProcessPrivate *priv;
	IrisProcess *process = IRIS_PROCESS (task);

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	switch (message->what) {
	case IRIS_TASK_MESSAGE_CANCEL:
		handle_cancel (process, message);
		break;

	case IRIS_TASK_MESSAGE_EXECUTE:
		handle_execute (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_NO_MORE_WORK:
		handle_no_more_work (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_ADD_SOURCE:
		handle_add_source (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_ADD_SINK:
		handle_add_sink (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_ADD_WATCH_CALLBACK:
		handle_add_watch_callback (process, message);
		break;

	default:
		IRIS_TASK_CLASS (iris_process_parent_class)->handle_message (IRIS_TASK (process),
		                                                             message);
		break;
	}
}

static void
iris_task_post_work_item_real (IrisProcess *process,
                               IrisMessage *work_item)
{
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_TASK (process));

	priv = process->priv;

	iris_queue_push (priv->work_queue, work_item);
}

/**************************************************************************
 *                 IrisTask Class VTable Implementations                  *
 **************************************************************************/

static void
iris_process_post_work_item (IrisMessage *work_item,
                             gpointer     data)
{
	IrisProcess *process;

	g_return_if_fail (IRIS_IS_PROCESS (data));

	process = IRIS_PROCESS (data);
	IRIS_PROCESS_GET_CLASS (process)->post_work_item (process, work_item);
}

#define UPDATE_WATCHES()                                              \
	for (node=priv->watch_callback_list; node; node=node->next) {     \
		IrisProcessWatchClosure *state;                               \
		state = g_slice_new (IrisProcessWatchClosure);                \
                                                                      \
		state->task = task;                                           \
		state->completed = g_atomic_int_get (&priv->processed_items); \
		state->total = g_atomic_int_get (&priv->total_items);         \
		state->cancelled = cancelled;                                 \
                                                                      \
		state->watch_callback = node->data;                           \
		g_idle_add (progress_watch_idle_callback, state);             \
	}

static void
iris_process_execute_real (IrisTask *task)
{
	GValue    params[2] = { {0,}, {0,} };
	GList    *node;
	gboolean  cancelled;
	IrisProcess        *process;
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (task));

	process = IRIS_PROCESS (task);
	priv = process->priv;

	g_value_init (&params[0], G_TYPE_OBJECT);
	g_value_set_object (&params[0], process);
	g_value_init (&params[1], G_TYPE_POINTER);

	/* Lock-free queue, so pop and timed_pop should be avoided. We need a
	 * timeout anyway to check for cancellation.
	 */
	while (1) {
		IrisMessage *work_item;

		cancelled = iris_process_is_canceled (process);

		/* Update progress monitors, no more than four times a second */
		if (g_timer_elapsed (priv->watch_callback_timer, NULL) > 0.250) {
			g_timer_reset (priv->watch_callback_timer);
			UPDATE_WATCHES();
		}

		if (cancelled)
			break;

		work_item = iris_queue_try_pop (priv->work_queue);

		if (!work_item) {
			if (iris_process_has_predecessor (process)) {
				if (iris_process_is_finished (process))
					break;
			} else if (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_NO_MORE_WORK)) {
					if ((g_atomic_int_get (&priv->processed_items)) ==
						(g_atomic_int_get (&priv->total_items)))
						break;
			}

			/* FIXME: would be nice if we could tell the scheduler "don't execute this for at least
			 * 20ms" or something so we don't waste quite as much power waiting for work.
			 */
			iris_scheduler_queue (iris_scheduler_default (),
			                      (IrisCallback)iris_process_execute_real,
			                      process,
			                      NULL);
			return;
		}

		g_value_set_pointer (&params[1], work_item);
		g_closure_invoke (task->priv->closure, NULL, 2, params, NULL);

		iris_message_unref (work_item);

		g_atomic_int_inc (&priv->processed_items);
	};

	UPDATE_WATCHES();

	g_closure_invalidate (task->priv->closure);
	g_closure_unref (task->priv->closure);
	task->priv->closure = NULL;
	g_value_unset (&params[1]);
	g_value_unset (&params[0]);

	if (cancelled)
		return;

	// Execute callbacks and then mark finished.
	iris_task_complete (IRIS_TASK (process));
}


static void
iris_process_finalize (GObject *object)
{
	IrisProcess *process = IRIS_PROCESS (object);
	IrisProcessPrivate *priv = process->priv;
	GList *node;

	g_free (priv->title);

	g_timer_destroy (priv->watch_callback_timer);

	for (node=priv->watch_callback_list; node; node=node->next)
		g_closure_unref (node->data);
	g_list_free (priv->watch_callback_list);

	G_OBJECT_CLASS (iris_process_parent_class)->finalize (object);
}

static void
iris_process_class_init (IrisProcessClass *process_class)
{
	IrisTaskClass *task_class;
	GObjectClass  *object_class;

	process_class->post_work_item = iris_task_post_work_item_real;

	task_class = IRIS_TASK_CLASS (process_class);
	task_class->execute = iris_process_execute_real;
	task_class->handle_message = iris_process_handle_message_real;

	object_class = G_OBJECT_CLASS (process_class);
	object_class->finalize = iris_process_finalize;

	/**
	* IrisProcess::source-connected:
	* @process: the #IrisProcess
	* @predecessor: the source #IrisProcess
	*
	* The "source-connected" signal is emitted when a process is connected as a
	* source to @process.
	*
	* The signal is emitted in the GLib main loop thread using g_idle_add, so
	* it will only be emitted if the GLib main loop is running.
	*/
	process_signals[SOURCE_CONNECTED] =
	  g_signal_new (("source-connected"),
	                G_OBJECT_CLASS_TYPE (object_class),
	                G_SIGNAL_RUN_FIRST,
	                0,
	                NULL, NULL,
	                g_cclosure_marshal_VOID__OBJECT,
	                G_TYPE_NONE, 1,
	                IRIS_TYPE_PROCESS);

	/**
	* IrisProcess::sink-connected:
	* @process: the #IrisProcess
	* @successor: the sink #IrisProcess
	*
	* The "sink-connected" signal is emitted when @process is connected as a
	* a source to @successor.
	*
	* The signal is emitted in the GLib main loop thread using g_idle_add, so
	* it will only be emitted if the GLib main loop is running.
	*/
	process_signals[SINK_CONNECTED] =
	  g_signal_new (("sink-connected"),
	                G_OBJECT_CLASS_TYPE (object_class),
	                G_SIGNAL_RUN_FIRST,
	                0,
	                NULL, NULL,
	                g_cclosure_marshal_VOID__OBJECT,
	                G_TYPE_NONE, 1,
	                IRIS_TYPE_PROCESS);

	g_type_class_add_private (object_class, sizeof(IrisProcessPrivate));
}

static void
iris_process_init (IrisProcess *process)
{
	IrisProcessPrivate *priv;

	priv = process->priv = IRIS_PROCESS_GET_PRIVATE (process);

	/* FIXME: Leaked? "We should have a teardown port for dispose" */
	priv->work_port = iris_port_new ();
	priv->work_receiver = iris_arbiter_receive (NULL,
	                                            priv->work_port,
	                                            iris_process_post_work_item,
	                                            g_object_ref (process),
	                                            (GDestroyNotify)g_object_unref);
	iris_arbiter_coordinate (priv->work_receiver, NULL, NULL);

	priv->work_queue = iris_queue_new ();

	priv->source = NULL;
	priv->sink = NULL;

	priv->processed_items = 0;
	priv->total_items = 0;

	priv->title = NULL;

	priv->watch_callback_list = NULL;
	priv->watch_callback_timer = g_timer_new ();

	/* FIXME: very, very simplistic implementation .. */
	IrisScheduler *scheduler = iris_scheduler_default ();
	if (scheduler->maxed)
		g_warning ("Scheduler maxed.\n");
	iris_scheduler_add_thread (scheduler, iris_thread_new (TRUE));
}

static void
iris_process_dummy (IrisProcess *process, IrisMessage *work_item, gpointer user_data)
{
}
