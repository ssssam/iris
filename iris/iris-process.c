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
#include "iris-progress.h"

#include <stdio.h>

/**
 * SECTION:iris-process
 * @title: IrisProcess
 * @short_description: A concurrent and asynchronous process abstraction
 *
 * #IrisProcess is a work queue that operates on atomic work items. An example
 * of this is reading information from every file in a directory, or
 * processing a complex calculation with a series of inputs. This is a special
 * case of #IrisTask, which would better suited you want to execute one long
 * task such as downloading a single file.
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
 *
 * Internal note: because processes run asynchronously in their own threads,
 * the controlling functions communicate through messages and will return
 * before the message has been received by the process. This means, for
 * example, that the following code has undefined behaviour:
 * |[
 *   IrisProcess *process;
 *   process = iris_process_new_with_func (callback, NULL, NULL);
 *
 *   iris_process_run (process);
 *   g_assert (iris_process_is_executing (process));
 * ]|
 * The assert may fail or pass, depending on whether @process has received the
 * 'run' message. Messages are executed in order, so given that
 * iris_process_connect() can only be called before iris_process_run(), the
 * following code is valid:
 * |[
 *   IrisProcess *head, *tail;
 *   head = iris_process_new_with_func (callback_1, NULL, NULL);
 *   tail = iris_process_new_with_func (callback_1, NULL, NULL); *
 *
 *   iris_process_connect (head, tail);
 *   iris_process_run (head);
 *
 *   while (1)
 *     if (iris_process_is_executing (head)) {
 *       g_assert (iris_process_has_successor (head));
 *       break;
 *     }
 * ]|
 * Once the 'run' message has been executed, we know for sure that the
 * 'connect' message has also executed.
 */

/* How frequently the process checks for cancellation between try_pop calls. */
#define WAKE_UP_INTERVAL  20000

#define FLAG_IS_ON(p,f)  ((IRIS_TASK(p)->priv->flags & f) != 0)
#define FLAG_IS_OFF(p,f) ((IRIS_TASK(p)->priv->flags & f) == 0)
#define ENABLE_FLAG(p,f) G_STMT_START{IRIS_TASK(p)->priv->flags|=f;}G_STMT_END
#define DISABLE_FLAG(p,f) G_STMT_START{IRIS_TASK(p)->priv->flags&=~f;}G_STMT_END

G_DEFINE_TYPE (IrisProcess, iris_process, IRIS_TYPE_TASK);

static void             iris_process_dummy         (IrisProcess *task,
                                                    IrisMessage *work_item,
                                                    gpointer user_data);


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
 * runs out of work AND iris_process_no_more_work() (or iris_process_cancel())
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
 *
 * Remember that iris_process_connect() will return before the connection
 * actually takes place. However, because connections must be made before the
 * tasks execute, it is guaranteed that the connection has taken place once
 * iris_process_is_executing() returns %TRUE. It is also guaranteed to have
 * taken place if you are calling from the work function, of course. This
 * affects the functions iris_process_get_predecessor(),
 * iris_process_get_successor(), iris_process_has_predecessor() and
 * iris_process_has_successor().
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
}

/**
 * iris_process_is_executing:
 * @process: An #IrisProcess
 *
 * Checks if a process is executing.
 *
 * Return value: %TRUE if the process is executing.
 */
gboolean
iris_process_is_executing (IrisProcess *process)
{
	return iris_task_is_executing (IRIS_TASK (process));
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
 * Note: iris_process_connect() returns before the connection is actually
 * made. You should not call this function outside of the work function, unless
 * iris_process_is_executing() returns %TRUE. See above for more information.
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
 * Note: iris_process_connect() returns before the connection is actually
 * made. You should not call this function outside of the work function, unless
 * iris_process_is_executing() returns %TRUE. See above for more information.
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
 * Note: iris_process_connect() returns before the connection is actually
 * made. You should not call this function outside of the work function, unless
 * iris_process_is_executing() returns %TRUE. See above for more information.
 *
 * Return value: a pointer to the an #IrisProcess, or %NULL
 */
IrisProcess *
iris_process_get_predecessor (IrisProcess *process)
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
 * Note: iris_process_connect() returns before the connection is actually
 * made. You should not call this function outside of the work function, unless
 * iris_process_is_executing() returns %TRUE. See above for more information.
 *
 * Return value: a pointer to the an #IrisProcess, or %NULL
 */
IrisProcess *
iris_process_get_successor (IrisProcess *process)
{
	g_return_val_if_fail (IRIS_IS_PROCESS (process), NULL);

	return process->priv->sink;
}

/**
 * iris_process_get_title:
 * @process: An #IrisProcess
 *
 * Returns the title of @process, as set by iris_process_set_title().
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
 * process is doing, and will be used in any status dialogs that are watching
 * the process.
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
 * iris_process_add_watch:
 * @process: An #IrisProcess
 * @watch_port: An #IrisPort
 *
 * @watch_port will receive status updates from @process. The messages are
 * of the type #IrisProgressMessageType.
 *
 * The #IrisProgressMonitor interface and its implementors provide a more
 * high-level facility for progress monitoring.
 */
void
iris_process_add_watch (IrisProcess               *process,
                        IrisPort                  *watch_port)
{
	IrisMessage *message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (IRIS_IS_PORT (watch_port));

	message = iris_message_new_data (IRIS_PROCESS_MESSAGE_ADD_WATCH,
	                                 IRIS_TYPE_PORT, watch_port);
	iris_port_post (IRIS_TASK(process)->priv->port, message);
};


/**************************************************************************
 *                      IrisProcess Internal Helpers                      *
 *************************************************************************/

static void
post_progress_message (IrisProcess *process,
                       IrisMessage *progress_message)
{
	GList    *node;
	IrisPort *watch_port;
	IrisProcessPrivate *priv;

	priv = process->priv;

	for (node=priv->watch_port_list; node; node=node->next) {
		watch_port = IRIS_PORT (node->data);
		iris_port_post (watch_port, progress_message);
	}
};

static void
update_status (IrisProcess *process)
{
	IrisProcessPrivate *priv;
	IrisMessage        *message;
	int                 total;

	priv = process->priv;

	/* Send total items first, so we don't risk processed_items > total_items */
	total = g_atomic_int_get (&priv->total_items);

	if (priv->total_items_pushed < total) {
		priv->total_items_pushed = total;

		message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS,
	                                     G_TYPE_INT,
	                                     total);
		post_progress_message (process, message);
		iris_message_unref (message);
	}

	/* Now send processed items */
	message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS,
	                                 G_TYPE_INT,
	                                 g_atomic_int_get (&priv->processed_items));
	post_progress_message (process, message);
	iris_message_unref (message);
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
handle_finish (IrisProcess *process,
               IrisMessage *message)
{
	IrisProcessPrivate *priv;
	IrisMessage        *progress_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	if (FLAG_IS_ON (IRIS_TASK (process), IRIS_TASK_FLAG_FINISHED))
		return;

	/* Send 'complete' to any watchers, now that iris_process_is_finished() will
	 * return TRUE.
	 */
	if (priv->watch_port_list != NULL) {
		progress_message = iris_message_new (IRIS_PROGRESS_MESSAGE_COMPLETE);
		post_progress_message (process, progress_message);
		iris_message_unref (progress_message);
	}

	/* Chain up */
	IRIS_TASK_CLASS (iris_process_parent_class)->handle_message
	  (IRIS_TASK (process), message);
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

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	data = iris_message_get_data (message);
	source_process = g_value_get_object (data);

	g_return_if_fail (IRIS_IS_PROCESS (source_process));

	priv = process->priv;
	priv->source = source_process;

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_HAS_SOURCE);
}

static void
handle_add_sink (IrisProcess *process,
                 IrisMessage *message)
{
	IrisProcessPrivate           *priv;
	const GValue                 *data;
	IrisProcess                  *sink_process;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	data = iris_message_get_data (message);
	sink_process = g_value_get_object (data);

	g_return_if_fail (IRIS_IS_PROCESS (sink_process));

	priv = process->priv;
	priv->sink = sink_process;

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_HAS_SINK);
}

static void
handle_add_watch (IrisProcess *process,
                  IrisMessage *message)
{
	IrisProcessPrivate *priv;
	const GValue       *data;
	IrisPort           *watch_port;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	priv = process->priv;

	data = iris_message_get_data (message);
	watch_port = IRIS_PORT (g_value_get_object (data));

	g_object_ref (watch_port);
	priv->watch_port_list = g_list_append (priv->watch_port_list,
	                                       watch_port);

	/* Send a status message now, it's possible that the process has actually
	 * already completed and so this may be only status message that the watcher
	 * receives.
	 */
	update_status (process);

	if (iris_process_is_finished (process)) {
		message = iris_message_new (IRIS_PROGRESS_MESSAGE_COMPLETE);
		post_progress_message (process, message);
		iris_message_unref (message);
	}
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

	/* Note that COMPLETE means the work is done, while FINISH comes after when
	 * all callbacks have executed as well. IRIS_TASK_FLAG_FINISHED is not set
	 * (hence iris_process_is_finished will return FALSE) until this second
	 * message is sent.
	 */
	case IRIS_TASK_MESSAGE_FINISH:
		handle_finish (process, message);
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

	case IRIS_PROCESS_MESSAGE_ADD_WATCH:
		handle_add_watch (process, message);
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


static void
iris_process_execute_real (IrisTask *task)
{
	GValue    params[2] = { {0,}, {0,} };
	gboolean  cancelled;
	IrisProcess        *process;
	IrisProcessPrivate *priv;
	IrisMessage        *message;

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
		if (priv->watch_port_list != NULL &&
		    g_timer_elapsed (priv->watch_timer, NULL) >= 0.250) {
			g_timer_reset (priv->watch_timer);
			update_status (process);
		}

		if (cancelled)
			break;

		work_item = iris_queue_try_pop (priv->work_queue);

		if (!work_item) {
			if (iris_process_has_predecessor (process)) {
				if (iris_process_is_finished (process))
					break;
			}
			else if (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_NO_MORE_WORK)) {
				/* No races possible: only recursive work items can post more work if the above flag
				 * is set, and they must do so before they are marked as complete. Therefore
				 * 'processed' can never reach 'total' before the last work item completes.
				 */
				if ((g_atomic_int_get (&priv->processed_items)) ==
					(g_atomic_int_get (&priv->total_items)))
					break;
			}

			/* Yield, by reposting this function to the scheduler and returning.
			 * FIXME: would be nice if we could tell the scheduler "don't execute this for at least
			 * 20ms" or something so we don't waste quite as much power waiting for work.
			 */
			iris_scheduler_queue (iris_scheduler_default (),
			                      (IrisCallback)iris_process_execute_real,
			                      process,
			                      NULL);
			return;
		}

		/* Execute work item */
		g_value_set_pointer (&params[1], work_item);
		g_closure_invoke (task->priv->closure, NULL, 2, params, NULL);

		iris_message_unref (work_item);

		g_atomic_int_inc (&priv->processed_items);
	};

	g_closure_invalidate (task->priv->closure);
	g_closure_unref (task->priv->closure);
	task->priv->closure = NULL;
	g_value_unset (&params[1]);
	g_value_unset (&params[0]);

	if (priv->watch_port_list != NULL) {
		update_status (process);

		if (cancelled) {
			message = iris_message_new (IRIS_PROGRESS_MESSAGE_CANCELLED);
			post_progress_message (process, message);
			iris_message_unref (message);
		}
	}

	if (!cancelled)
		// Execute callbacks and then mark finished.
		iris_task_complete (IRIS_TASK (process));

	/* IRIS_PROGRESS_MESSAGE_COMPLETE will be sent when
	 * IRIS_TASK_MESSAGE_FINISH is received; ie. when all callbacks have
	 * executed.
	 */
}


static void
iris_process_finalize (GObject *object)
{
	IrisProcess *process = IRIS_PROCESS (object);
	IrisProcessPrivate *priv = process->priv;
	GList *node;

	g_free (priv->title);

	for (node=priv->watch_port_list; node; node=node->next)
		g_object_unref (IRIS_PORT (node->data));
	g_list_free (priv->watch_port_list);

	g_timer_destroy (priv->watch_timer);

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
	priv->total_items_pushed = 0;

	priv->title = NULL;

	priv->watch_port_list = NULL;
	priv->watch_timer = g_timer_new ();

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
