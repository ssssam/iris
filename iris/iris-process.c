/* iris-process.c
 *
 * Copyright (C) 2009-11 Sam Thursfield <ssssam@gmail.com>
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

#include "iris-debug.h"
#include "iris-receiver-private.h"
#include "iris-task-private.h"
#include "iris-process.h"
#include "iris-process-private.h"
#include "iris-progress.h"

/**
 * SECTION:iris-process
 * @title: IrisProcess
 * @short_description: A concurrent and asynchronous process abstraction
 *
 * #IrisProcess is a specialisation of #IrisTask that operates on a set of work
 * items. A good example is the reading of information from each file in a
 * directory, or applying a transform function to a set of images.
 *
 * Once the #IrisProcess is created, you can give it work to do with
 * iris_process_enqueue(). The order in which the work items are executed is
 * unspecified. You can begin processing the work at any point with
 * iris_process_run(), and cancel at any point with iris_process_cancel().
 *
 * The process will be destroyed automatically by default (see #IrisTask for
 * more information on how this works). However, a process will
 * <emphasis>never</emphasis> finish before iris_process_no_more_work() has
 * been called. If running, it will spin waiting for more work until you call
 * iris_process_no_more_work(). If canceled, the process will cease execution,
 * but calls to iris_process_enqueue() and iris_process_run() will still
 * succeed (but not actually do anything). It is an error to call any other
 * methods of #IrisProcess after calling iris_process_cancel().
 *
 * The reason for this feature is to ensure synchronisation. For example, the
 * following code is flawed:
 * |[
 *   if (! iris_process_was_canceled (process)
 *     iris_process_enqueue (process, work_item);
 * ]|
 * You guessed it - the process might be canceled from another thread, after
 * you checked but before you tried to push some work. There is normally no need
 * to enqueue work after calling iris_process_run(), but if you do require this
 * and the process may be canceled from another thread, this is the correct way
 * to implement it:
 *
 * <example>
 * <title>Enqueuing work in an IrisProcess while the process is executing</title>
 * <programlisting>
 *   gboolean enqueue_some_work (IrisProcess *process,
 *                               GList      *work_items) {
 *       GList *node;
 *
 *       if (iris_process_was_canceled (process))
 *           /&ast; Allow process to finalize. Caller must not enqueue any more work &ast;/
 *           iris_process_no_more_work (process);
 *           return FALSE;
 *       }
 *
 *       for (node=work_items; node; node=node->next)
 *           iris_process_enqueue (process, node->data);
 *
 *       return TRUE;
 *   }
 * </programlisting>
 * </example>
 * The progress of an #IrisProcess, or a chained group of them, can be monitored
 * using some kind of #IrisProgressMonitor, such as an #GtkIrisProgressDialog.
 * If you want to do this you may want to call iris_process_set_title() to give
 * the process a label. Note that progress update messages might not be sent
 * until iris_process_run() is called.
 *
 * <refsect2 id="chaining">
 * <title>Process connections</title>
 * <para>
 * Processes can be chained together, so that the results of one are queued in
 * another for further processing. After calling iris_process_connect(), the
 * <firstterm>source</firstterm> process can forward work using
 * iris_process_forward().
 *
 * You should enqueue work in the process at the head of the chain, calling
 * iris_process_no_more_work() as normal, and should start the chain by calling
 * iris_process_run() <emphasis>on the head process</emphasis>. The later
 * processes will each run until their source process completes work, so the
 * work will flow down the chain automatically.
 *
 * Once the chain is running, it is only necessary to hold a reference on the
 * last process in the chain. Callbacks should be added to this process to run
 * when the whole chain has completed processing. iris_process_is_finished()
 * will only return %TRUE for the tail process once the whole chain has
 * completed or been canceled. If any process in the chain is canceled, the
 * whole chain will stop. Each process 'owns' its source process, so the
 * chain will not be freed until the last reference of the tail process is
 * removed.
 *
 * You cannot pass connected processes to iris_task_add_dependency() (this
 * wouldn't make sense, as processes connected with iris_process_connect() are
 * intended to be running simultaneously. It is not possible to disconnect
 * processes or connect them after they have started running. Also, a process
 * can currently only have one source and one sink process.
 * </refsect2>
 *
 * <refsect2 id="synchronisation">
 * <title>Synchronisation</title>
 * <para>
 * Because processes run asynchronously in their own threads,
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
 * </para></refsect2>
 */

/* How frequently the process checks for cancellation between try_pop calls. */
#define WAKE_UP_INTERVAL  20000

#define FLAG_IS_ON(p,f)  ((IRIS_TASK(p)->priv->flags & f) != 0)
#define FLAG_IS_OFF(p,f) ((IRIS_TASK(p)->priv->flags & f) == 0)
#define ENABLE_FLAG(p,f) G_STMT_START{IRIS_TASK(p)->priv->flags|=f;}G_STMT_END
#define DISABLE_FLAG(p,f) G_STMT_START{IRIS_TASK(p)->priv->flags&=~f;}G_STMT_END

G_DEFINE_TYPE (IrisProcess, iris_process, IRIS_TYPE_TASK);

enum {
	PROP_0,
	PROP_TITLE
};

static void             post_output_estimate         (IrisProcess *process);

static void             post_progress_message        (IrisProcess *process,
                                                      IrisMessage *progress_message);

static void             iris_process_dummy           (IrisProcess *task,
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
 * runs out of work AND iris_process_no_more_work() is called. You can abort
 * execution by calling iris_process_cancel(), but you must still call
 * iris_process_no_more_work() for the cancel to take effect.
 *
 * If @process has already been canceled, this function will do nothing.
 *
 * If @process has any successors, they will also start. For more information,
 * see iris_process_connect().
 */
/* FIXME: part of me thinks this should only start the process in question,
 * and iris_process_run_all() should start the chain ...
 */
void
iris_process_run (IrisProcess *process)
{
	g_return_if_fail (IRIS_IS_PROCESS (process));

	iris_task_run (IRIS_TASK (process));
}

/**
 * iris_process_cancel:
 * @process: An #IrisProcess
 *
 * Cancels a process. The process work function will not be called again, and
 * if iris_process_no_more_work() was called @process will be freed (unless
 * someone holds a reference on it). Otherwise, @process will be freed once
 * that function is called.
 *
 * It is allowed to call iris_process_enqueue() after calling
 * iris_process_cancel(), if iris_process_no_more_work() was not yet called.
 * You may also call iris_process_run(), which will do nothing on a canceled
 * process. It is <emphasis>not</emphasis> permitted to call most other
 * functions. See individual docs for details.
 *
 * If the work function is slow, it should periodically check
 * iris_task_was_canceled() while working and quit execution if it returns
 * %TRUE.
 *
 * If @process is part of a chain of processes, this function will cancel the
 * whole chain.
 */
void
iris_process_cancel (IrisProcess *process)
{
	iris_task_cancel (IRIS_TASK (process));
}

/**
 * iris_process_connect:
 * @head: An #IrisProcess that is not running or canceled
 * @tail: An #IrisProcess that is not running or canceled
 *
 * Connects @head to @tail, so @tail receives its events from @head when it is
 * done processing them. @head and @tail must not be running or canceled. See
 * <link linkend="chaining">the introduction</link> for more information.
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

	if (FLAG_IS_ON (head, IRIS_TASK_FLAG_STARTED) ||
	    FLAG_IS_ON (tail, IRIS_TASK_FLAG_STARTED)) {
		g_warning ("iris_process_connect: %s process is already running.\n"
		           "Processes and tasks are immutable once executing or "
		           "canceled.",
		           FLAG_IS_ON (head, IRIS_TASK_FLAG_WORK_ACTIVE) ? "head"
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

	tail_message = iris_message_new_data (IRIS_PROCESS_MESSAGE_ADD_SOURCE,
	                                      IRIS_TYPE_PROCESS, head);
	iris_port_post (tail_task_priv->port, tail_message);
}

/**
 * iris_process_enqueue:
 * @process: An #IrisProcess
 * @work_item: An #IrisMessage. The process will sink the floating reference or
 *             add a new reference.
 *
 * Posts a work item to the queue. The task function will be passed @work_item
 * when the work item is executed. The type and contents of the message are
 * entirely up to you. The work items should not depend in any way on order of
 * execution - if this is a problem, you should create individual #IrisTask
 * objects which will allow you to specify dependencies.
 *
 * The caller does not need to unref @work_item once it has been enqueued, in
 * normal cases. The message and its data will be freed after the work item is
 * processed or cancelled. Note that you should not free data associated with
 * the work items in the process work function because of the possibility of
 * cancelling; using iris_message_set_pointer_full() allows any type of data to
 * be implicitly freed when no longer needed.
 *
 * If @process has been cancelled, this function will do nothing. It is an
 * error to call this function once iris_process_no_more_work() has been
 * called.
 */
void
iris_process_enqueue (IrisProcess *process,
                      IrisMessage *work_item)
{
	IrisProcessPrivate *priv;
	gint                total_items, estimated_total_items;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	if (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_NO_MORE_WORK) &&
	    !FLAG_IS_ON (process, IRIS_TASK_FLAG_CANCELED)) {
		g_warning ("process %lx: cannot enqueue more work items, "
		           "because iris_process_no_more_work() has been called "
		           "(or there is a bug in process connections)",
		           (gulong)process);
		return;
	};

	if (FLAG_IS_ON (process, IRIS_TASK_FLAG_CANCELED)) {
		/* Don't enqueue more work if the process has been canceled */
		iris_message_ref_sink (work_item);
		iris_message_unref (work_item);
		return;
	}

	g_atomic_int_inc (&priv->total_items);

	total_items = g_atomic_int_get (&priv->total_items);
	estimated_total_items = g_atomic_int_get (&priv->estimated_total_items);

	/* Don't allow estimated total inputs to be lower than the real value */
	if (total_items > estimated_total_items)
		g_atomic_int_set (&priv->estimated_total_items, total_items);

	iris_port_post (priv->work_port, work_item);

	/* FIXME: it's bad that we call this function on every queued work item.
	 * The best way I can think of to avoid this is to implement
	 * iris_scheduler_queue_timeout() and send the message from a timeout. You
	 * can see why I haven't done that yet :)
	 */
	post_output_estimate (process);
};


/**
 * iris_process_forward:
 * @process: An #IrisProcess that is currently executing
 * @work_item: An #IrisMessage
 *
 * This function is for use during the process' work function. When called, it
 * forwards @work_item to the successor of @process. @process and its successor
 * must have been connected using iris_process_connect(), and @process must be
 * running and not canceled.
 *
 * @work_item can be the same message that was passed to the calling work
 * function, and references will be correctly managed.
 */
void
iris_process_forward (IrisProcess *process,
                      IrisMessage *work_item)
{
	/* No synchronisation required here: 'sink' can only be changed before the
	 * process starts to execute, and the sink cannot finish (either for
	 * completion or cancelation) until this process has finished.
	 */
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (iris_process_has_successor (process));
	g_return_if_fail (iris_process_is_executing (process));

	priv = process->priv;

	if (iris_process_was_canceled (process)) {
		/* This is possible in a chain of 3 or more processes: the head can be
		 * pushing work while the tail has been canceled, and the middle
		 * process has processed DEP_CANCELED but head process hasn't done yet.
		 */
		iris_message_ref_sink (work_item);
		iris_message_unref (work_item);
		return;
	}

	iris_process_enqueue (priv->sink, work_item);
}

/**
 * iris_process_recurse:
 * @process: An #IrisProcess that is currently executing
 * @work_item: An #IrisMessage
 *
 * This function is for use during the process' work function. When called, it
 * enqueues @work_item in @process, which must be running and not canceled.
 *
 * You might want to do this if you have a process which operates on
 * directories and their subdirectories recursively, for example. In this
 * situation, iris_process_enqueue() would fail if you had called
 * iris_process_no_more_work() already.
 */
/* FIXME: same applies as iris_process_forward() */
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
	g_return_if_fail (iris_process_is_executing (process));
	g_return_if_fail (! iris_process_was_canceled (process));

	priv = process->priv;

	g_atomic_int_inc (&priv->total_items);

	iris_port_post (priv->work_port, work_item);
}

/**
 * iris_process_no_more_work:
 * @process: An #IrisProcess
 *
 * Causes @process to finish up its remaining work and then destroy itself. Or,
 * if @process has been canceled, to finish immediately. If this function is
 * never called, @process will stay around idling and using a substantial
 * amount of CPU cycles forever.
 *
 * Any processes chained to the output of @process will also stop once they run
 * out of work and @process has completed, as they expect to obtain all of
 * their work items from @process.
 *
 * iris_process_no_more_work() cannot be called on processes that are connected
 * to a source process. These will run for as long as their source processes
 * have work to do.
 */
void
iris_process_no_more_work (IrisProcess *process)
{
	IrisTaskPrivate *task_priv = IRIS_TASK(process)->priv;

	g_return_if_fail (! iris_process_has_predecessor (process));

	IrisMessage *message = iris_message_new (IRIS_PROCESS_MESSAGE_NO_MORE_WORK);
	iris_port_post (task_priv->port, message);
};

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
 * iris_process_is_finished:
 * @process: An #IrisProcess
 *
 * Checks if a process has succeeded, thrown a fatal error or been
 * canceled. If @process has been canceled but iris_process_no_more_work()
 * has not yet been called, this function will return %FALSE.
 *
 * If @process has a source process connected, this function will never return
 * %TRUE unless the source is also finished (this is significant when the sink
 * process is canceled - it will wait for the source to process the cancel
 * before it finishes itself).
 *
 * Return value: %TRUE if the process will not process any more work
 */
gboolean
iris_process_is_finished (IrisProcess *process)
{
	return iris_task_is_finished (IRIS_TASK (process));
}

/**
 * iris_process_was_canceled:
 * @process: An #IrisProcess
 *
 * Checks if a process has been canceled.  Note that if @process handled the
 * cancel and chose to ignore it, %FALSE will be returned (this can only be
 * done by subclasses of #IrisProcess).
 *
 * If @process has been canceled but iris_process_no_more_work() was not yet
 * called, this function will still return %TRUE.
 *
 * Return value: %TRUE if the process was canceled.
 */
gboolean
iris_process_was_canceled (IrisProcess *process)
{
	return iris_task_was_canceled (IRIS_TASK (process));
}

/**
 * iris_process_has_succeeded:
 * @process: An #IrisProcess
 *
 * Checks to see if the process has completed all of its work, and had
 * iris_process_no_more_work() called on it.
 *
 * If @process is chained to a source process, it has completed when it has
 * finished all of its work and the first process in the chain has completed.
 *
 * Return value: %TRUE if the process has completed its work successfully
 */
gboolean
iris_process_has_succeeded (IrisProcess *process)
{
	IrisProcessPrivate *priv;

	g_return_val_if_fail (IRIS_IS_PROCESS (process), FALSE);

	priv = process->priv;

	if (!iris_process_has_predecessor (process))
		return iris_task_has_succeeded (IRIS_TASK (process));
	else
		return iris_process_has_succeeded (priv->source) &&
		       iris_task_has_succeeded (IRIS_TASK (process));
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

	return g_atomic_pointer_get (&process->priv->title);
}

/**
 * iris_process_get_status:
 * @process: An #IrisProcess
 * @p_processed_items: Location to store the number of items completed by
 *                     @process, or %NULL
 * @p_total_items: Location to store the total number of items that @process
 *                 will handle.
 *
 * Gets the status of @process, returning how much work has been done and the
 * total amount of work. Note that the total amount of work is likely to be an
 * estimate if @process is still receiving work items from another process,
 * since the its predecessor in this chain may not know exactly how many times
 * itwill call iris_process_forward(). For more information, see
 * iris_process_set_output_estimation().
 */
void
iris_process_get_status (IrisProcess *process,
                         gint        *p_processed_items,
                         gint        *p_total_items)
{
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	if (p_processed_items != NULL)
		*p_processed_items = g_atomic_int_get (&priv->processed_items);

	if (p_total_items != NULL) {
		*p_total_items = g_atomic_int_get (&priv->estimated_total_items);
		if (*p_total_items == 0)
			*p_total_items = g_atomic_int_get (&priv->total_items);
	}
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
 * iris_process_set_func:
 * @process: An #IrisProcess
 * @func: An #IrisProcessFunc to call for each work item.
 * @user_data: user data for @func
 * @notify: An optional #GDestroyNotify or %NULL
 *
 * Sets the work function of @process. This function cannot be called after
 * iris_process_run() or iris_process_cancel().
 */
void
iris_process_set_func (IrisProcess     *process,
                       IrisProcessFunc  func,
                       gpointer         user_data,
                       GDestroyNotify   notify)
{
	GClosure    *closure;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	/*g_return_if_fail (FLAG_IS_OFF (process, IRIS_TASK_FLAG_EXECUTING));*/

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
 * iris_process_run() or iris_process_cancel().
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
	IrisMessage        *message;
	gchar              *old_title;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	old_title = g_atomic_pointer_get (&priv->title);
	g_atomic_pointer_set (&priv->title, g_strdup (title));
	g_free (old_title);

	if (priv->watch_port_list != NULL) {
		message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_TITLE,
		                                 G_TYPE_STRING,
		                                 title);
		post_progress_message (process, message);
	}
}


/**
 * iris_process_set_output_estimation:
 * @process: An #IrisProcess
 * @factor: ratio between number of input items and estimated number of output
 *          items. (Default: 1.0)
 *
 * When processes are chained together, it is hard to show progress widgets for
 * the later ones because they don't know how many items they will have to
 * process. Without some estimation, they only know how many have been
 * forwarded from their predecessor, which leads to their progress bar jumping
 * to 50% and going steadily backwards as more work items are forwarded.
 *
 * For this reason, processes track the <emphasis>estimated</emphasis> amount of
 * of work they have left as well as their actual queue. By default each process
 * sends an estimation to the next one in the queue, which is its queue length if it
 * is the first process, or its estimated total work if it is further down the
 * chain. You may alter the calculation using @factor. For example, if each input
 * normally generates two output work items, set it to 2.0.
 */
/* FIXME: when processes track their own progress display mode, should we force
 * chains that don't do estimation to IRIS_PROGRESS_ACTIVITY_ONLY ??
 */
/* A quick API design note: I considered much more powerful ways of estimation,
 * but decided it was needless. You could have a hook/signal called from
 * iris_process_enqueue(), which could look at the work item and decide how
 * many work items it will produce. However, the best use case I could come up
 * with was some sort of demuxing, where either the work item could be parsed and
 * split into the separate elements in the enqueue callback (which removes the
 * need for the process, and could just be done before the items were enqueued)
 * or the calculation of how many items will result from one input is too
 * involved to be estimated easily. 
 */
void
iris_process_set_output_estimation (IrisProcess *process,
                                    gfloat       factor)
{
	IrisProcessPrivate *priv;
	gfloat             *p_old_factor, *p_new_factor;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (factor > 0.0);

	priv = process->priv;

	/* Set now, in a nasty atomic way. We post the estimates from
	 * iris_process_enqueue() so it's vital to keep the factor updated in time
	 */
	p_new_factor = g_slice_new (float);
	*p_new_factor = factor;

	while (1) {
		p_old_factor = g_atomic_pointer_get (&priv->output_estimate_factor);
		if (g_atomic_pointer_compare_and_exchange
		      ((gpointer *)&priv->output_estimate_factor,
		       p_old_factor, p_new_factor))
			break;
	}

	g_slice_free (gfloat, p_old_factor);

	post_output_estimate (process);
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

	g_object_ref (watch_port);
	message = iris_message_new_data (IRIS_PROCESS_MESSAGE_ADD_WATCH,
	                                 IRIS_TYPE_PORT, watch_port);
	iris_port_post (IRIS_TASK(process)->priv->port, message);
};


/**************************************************************************
 *                      IrisProcess Internal Helpers                      *
 *************************************************************************/

/* This must be MT-safe, it's called from iris_process_enqueue() */
static void
post_output_estimate (IrisProcess *process)
{
	IrisProcessPrivate *priv;
	IrisProcess        *sink;
	IrisMessage        *message;
	float              *p_factor;
	gint                our_total_items;
	gint                estimate;

	priv = process->priv;

	sink = iris_process_get_successor (process);

	if (sink == NULL)
		return;

	/* Always calculate from estimated_total_items if set; when the source
	 * process completes we update it to the actual value in update_status()
	 */
	our_total_items = g_atomic_int_get (&priv->estimated_total_items);

	if (our_total_items == 0)
		our_total_items = g_atomic_int_get (&priv->total_items);

	if (our_total_items == 0)
		return;

	p_factor = g_atomic_pointer_get (&priv->output_estimate_factor);

	estimate = our_total_items * (*p_factor);

	message = iris_message_new_data (IRIS_PROCESS_MESSAGE_CHAIN_ESTIMATE,
	                                 G_TYPE_INT, estimate);
	iris_port_post (IRIS_TASK (sink)->priv->port, message);
}

static void
post_progress_message (IrisProcess *process,
                       IrisMessage *progress_message)
{
	GList    *node;
	IrisPort *watch_port;
	IrisProcessPrivate *priv;

	priv = process->priv;

	g_return_if_fail (priv->watch_port_list != NULL);

	iris_message_ref (progress_message);

	for (node=priv->watch_port_list; node; node=node->next) {
		watch_port = IRIS_PORT (node->data);
		iris_port_post (watch_port, progress_message);
	}

	iris_message_unref (progress_message);
};

static void
update_status (IrisProcess *process, gboolean force)
{
	IrisProcessPrivate *priv;
	IrisProcess        *source;
	IrisMessage        *message;
	int                 total, processed;

	priv = process->priv;

	if (IRIS_TASK (process)->priv->progress_mode == IRIS_PROGRESS_ACTIVITY_ONLY)
		message = iris_message_new (IRIS_PROGRESS_MESSAGE_PULSE);
	else {
		source = iris_process_get_predecessor (process);
		total = g_atomic_int_get (&priv->total_items);

		if (source != NULL) {
				if (iris_process_has_succeeded (source))
				/* Quicker to do this than to check if we have done it */
				g_atomic_int_set (&priv->estimated_total_items, total);
			else
				total = g_atomic_int_get (&priv->estimated_total_items);
		}

		/* Send total items first, so we don't risk processed_items > total_items */
		if (force || priv->watch_total_items < total) {
			priv->watch_total_items = total;

			message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS,
			                                 G_TYPE_INT, total);
			post_progress_message (process, message);
		}

		/* Now send processed items */
		processed = g_atomic_int_get (&priv->processed_items);
		message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS,
		                                 G_TYPE_INT,
		                                 processed);
	}

	post_progress_message (process, message);
}

/**************************************************************************
 *                  IrisProcess Message Handling Methods                  *
 *************************************************************************/

static void
handle_start_work (IrisProcess *process,
                   IrisMessage *message)
{
	IrisProcessPrivate *priv;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	/* Start the process, and then start any successors. */
	IRIS_TASK_CLASS (iris_process_parent_class)->handle_message
	  (IRIS_TASK (process), message);

	if (iris_process_has_successor (process)) {
		if (! iris_task_is_executing (IRIS_TASK (priv->sink)))
			iris_process_run (priv->sink);
	}
}

static void
handle_callbacks_finished (IrisProcess *process,
                           IrisMessage *in_message)
{
	IrisProcessPrivate *priv;
	IrisMessage        *progress_message,
	                   *dep_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	if (FLAG_IS_ON (IRIS_TASK (process), IRIS_TASK_FLAG_FINISHED)) {
		return;
	}

	/* Send 'complete' to any process watchers, now that
	 * iris_process_is_finished() will return TRUE.
	 */
	if (priv->watch_port_list != NULL) {
		progress_message = iris_message_new (IRIS_PROGRESS_MESSAGE_COMPLETE);
		post_progress_message (process, progress_message);
	}

	if (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_HAS_SINK) && priv->sink != NULL) {
		dep_message = iris_message_new_data (IRIS_TASK_MESSAGE_REMOVE_OBSERVER,
		                                     IRIS_TYPE_TASK, process);
		iris_port_post (IRIS_TASK (priv->sink)->priv->port, dep_message);
	}

	/* Chain up */
	IRIS_TASK_CLASS (iris_process_parent_class)->handle_message
	  (IRIS_TASK (process), in_message);
}

/* We have to totally replace task's cancel handlers to avoid it posting the
 * FINISH_CANCEL message when no_more_work() has not yet been called.
 */
static void
handle_start_cancel (IrisProcess *process,
                     IrisMessage *start_message)
{
	IrisProcessPrivate *priv;
	IrisMessage        *message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (start_message != NULL);

	g_return_if_fail (FLAG_IS_OFF (process, IRIS_TASK_FLAG_CANCELED));

	priv = process->priv;

	#ifdef IRIS_TRACE_TASK
	g_print ("process %lx: got start-cancel\n", (gulong)process);
	#endif

	if (FLAG_IS_ON (process, IRIS_TASK_FLAG_CALLBACKS_ACTIVE))
		/* Too late to cancel, callbacks have started */
		return;

	if (! IRIS_TASK_GET_CLASS (process)->can_cancel (IRIS_TASK (process)))
		return;

	ENABLE_FLAG (process, IRIS_TASK_FLAG_CANCELED);
	DISABLE_FLAG (process, IRIS_TASK_FLAG_NEED_EXECUTE);

	if (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_HAS_SOURCE)) {
		/* If we have a source, FINISH_CANCEL is not sent until we receive
		 * CHAIN_CANCEL from the source. This is to ensure cancels are
		 * processed in order.
		 */

		/* Signal to the work function to quit (if it's running), because this
		 * doesn't get set by the user on chained processes.
		 */
		ENABLE_FLAG (process, IRIS_PROCESS_FLAG_NO_MORE_WORK);
	}
	else {
		if (FLAG_IS_OFF (process, IRIS_PROCESS_FLAG_NO_MORE_WORK));
			/* FINISH_CANCEL message will be sent when no_more_work is received */
		else
		if (FLAG_IS_ON (process, IRIS_TASK_FLAG_WORK_ACTIVE));
			/* FINISH_CANCEL message will be sent when work function exits. */
		else {
			message = iris_message_new (IRIS_TASK_MESSAGE_FINISH_CANCEL);
			iris_port_post (IRIS_TASK (process)->priv->port, message);
		}
	}

	if (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_HAS_SINK)) {
		/* Stop ourselves getting messages from the sink since we are
		 * finishing, and it will finish later
		 */
		message = iris_message_new_data (IRIS_TASK_MESSAGE_REMOVE_OBSERVER,
		                                 IRIS_TYPE_TASK, process);
		iris_port_post (IRIS_TASK (priv->sink)->priv->port, message);
	}

	iris_task_notify_observers (IRIS_TASK (process));
}

static void
handle_finish_cancel (IrisProcess *process,
                      IrisMessage *in_message)
{
	IrisProcessPrivate *priv;
	IrisMessage        *work_item,
	                   *out_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	#ifdef IRIS_TRACE_TASK
	g_print ("process %lx: got finish-cancel\n", (gulong)process);
	#endif

	/* Protect against double emission of this message, which is allowed so
	 * the work function doesn't need to synchronise when it stops
	 */
	if (! g_atomic_int_compare_and_exchange
	        (&IRIS_TASK (process)->priv->cancel_finished, FALSE, TRUE))
		return;

	/* Clean up work item queue, which should not have been done until now
	 * (enqueues etc. are allowed until iris_progress_no_more_work() is called,
	 * even after a cancel)
	 */
	g_return_if_fail (priv->work_port != NULL);
	g_return_if_fail (priv->work_receiver != NULL);

	if (priv->work_port != NULL) {
		iris_receiver_destroy (priv->work_receiver, FALSE, NULL, FALSE);
		g_object_unref (priv->work_port);

		priv->work_receiver = NULL;
		priv->work_port = NULL;
	}

	while ((work_item = iris_queue_try_pop (priv->work_queue)))
		iris_message_unref (work_item);

	ENABLE_FLAG (process, IRIS_TASK_FLAG_FINISHED);

	if (iris_process_has_successor (process)) {
		/* Trigger finish cancel in sink now we are done */
		out_message = iris_message_new (IRIS_PROCESS_MESSAGE_CHAIN_CANCEL);
		iris_port_post (IRIS_TASK (priv->sink)->priv->port, out_message);
	}

	out_message = iris_message_new (IRIS_TASK_MESSAGE_FINISH);
	iris_port_post (IRIS_TASK (process)->priv->port, out_message);
}

static void
handle_dep_finished (IrisProcess *process,
                     IrisMessage *message)
{
	IrisProcessPrivate *priv;
	IrisTask           *observed;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	if (FLAG_IS_ON (process, IRIS_TASK_FLAG_FINISHED))
		/* This is possible when for example our source completes while we get
		 * canceled, then it notifies us of completion but we already stopped
		 */
		return;

	priv = process->priv;
	observed = g_value_get_object (iris_message_get_data (message));

	if ((priv->source == NULL || IRIS_TASK (priv->source) != observed) &&
	    (priv->sink == NULL || IRIS_TASK (priv->sink) != observed)) {
		/* Must be a task dependency */
		IRIS_TASK_CLASS (iris_process_parent_class)->handle_message
		  (IRIS_TASK (process), message);
		return;
	}

	#ifdef IRIS_TRACE_TASK
	g_print ("process %lx: dep-finished from %lx\n",
	         (gulong)process, (gulong)observed);
	#endif

	if (priv->source != NULL && IRIS_TASK (priv->source) == observed) {
		g_warn_if_fail (FLAG_IS_OFF (process, IRIS_TASK_FLAG_FINISHED));

		ENABLE_FLAG (process, IRIS_PROCESS_FLAG_NO_MORE_WORK);
	}
}

static void
handle_dep_canceled (IrisProcess *process,
                     IrisMessage *in_message)
{
	IrisProcessPrivate *priv;
	IrisTask           *observed;
	IrisMessage        *out_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (in_message != NULL);

	if (FLAG_IS_ON (process, IRIS_TASK_FLAG_CANCELED) ||
	    FLAG_IS_ON (process, IRIS_TASK_FLAG_FINISHED))
		return;

	priv = process->priv;
	observed = g_value_get_object (iris_message_get_data (in_message));

	if ((priv->source == NULL || IRIS_TASK (priv->source) != observed) &&
	    (priv->sink == NULL || IRIS_TASK (priv->sink) != observed)) {
		/* Must be a task dependency */
		IRIS_TASK_CLASS (iris_process_parent_class)->handle_message
		  (IRIS_TASK (process), in_message);
		return;
	}

	#ifdef IRIS_TRACE_TASK
	g_print ("process %lx: dep-canceled from %lx\n",
	         (gulong)process, (gulong)observed);
	#endif

	if (priv->source != NULL && IRIS_TASK (priv->source) == observed)
		handle_start_cancel (process, in_message);

	if (priv->sink != NULL && IRIS_TASK (priv->sink) == observed) {
		if (FLAG_IS_OFF (process, IRIS_TASK_FLAG_FINISHED))
			handle_start_cancel (process, in_message);
		else {
			out_message = iris_message_new (IRIS_PROCESS_MESSAGE_CHAIN_CANCEL);
			iris_port_post (IRIS_TASK (priv->sink)->priv->port, out_message);
		}
	}
}

static void
handle_no_more_work (IrisProcess *process,
                     IrisMessage *message)
{
	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	g_return_if_fail (FLAG_IS_OFF (process, IRIS_PROCESS_FLAG_NO_MORE_WORK));

	g_return_if_fail (FLAG_IS_OFF (process, IRIS_PROCESS_FLAG_HAS_SOURCE));

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_NO_MORE_WORK);

	if (iris_process_was_canceled (process)) {
		if (FLAG_IS_ON (process, IRIS_TASK_FLAG_WORK_ACTIVE));
			/* Wait until work function notices cancel to free object etc. */
		else {
			message = iris_message_new (IRIS_TASK_MESSAGE_FINISH_CANCEL);
			iris_port_post (IRIS_TASK (process)->priv->port, message);
		}
	}
}

/* When adding connections, the source/sink may already be running (perhaps
 * they have different control schedulers, for example). This shouldn't make
 * any difference in practice.
 */

static void
handle_add_source (IrisProcess *process,
                   IrisMessage *message)
{
	IrisProcessPrivate *priv;
	const GValue       *data;
	IrisProcess        *source_process;
	IrisMessage        *observer_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	priv = process->priv;

	data = iris_message_get_data (message);
	source_process = g_value_get_object (data);

	g_return_if_fail (IRIS_IS_PROCESS (source_process));
	g_warn_if_fail (FLAG_IS_OFF (process, IRIS_TASK_FLAG_STARTED));

	/* FIXME: check source is not in task deps, since that would break stuff */

	priv->source = g_object_ref (source_process);

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_HAS_SOURCE);

	/* Have the source process tell us when it's canceled/completed */
	observer_message = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_OBSERVER,
	                                          IRIS_TYPE_TASK, process);
	iris_port_post (IRIS_TASK (source_process)->priv->port, observer_message);
}

static void
handle_add_sink (IrisProcess *process,
                 IrisMessage *message)
{
	IrisProcessPrivate *priv;
	const GValue       *data;
	IrisProcess        *sink_process;
	IrisMessage        *observer_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	priv = process->priv;
	data = iris_message_get_data (message);
	sink_process = g_value_get_object (data);

	g_return_if_fail (IRIS_IS_PROCESS (sink_process));
	g_warn_if_fail (FLAG_IS_OFF (process, IRIS_TASK_FLAG_STARTED));

	/* FIXME: check sink is not in task deps, that would ruin everything */

	priv->sink = sink_process;

	ENABLE_FLAG (process, IRIS_PROCESS_FLAG_HAS_SINK);

	/* Have the source process tell us when it's canceled/completed */
	observer_message = iris_message_new_data (IRIS_TASK_MESSAGE_ADD_OBSERVER,
	                                          IRIS_TYPE_TASK, process);
	iris_port_post (IRIS_TASK (sink_process)->priv->port, observer_message);

	post_output_estimate (process);
}

/* CHAIN_CANCEL: sent to sink by source process when it completes its cancel.
 * This ensures FINISH_CANCEL is processed in order going down the chain, which
 * in turn means that iris_process_is_finished(tail_process) will not return
 * TRUE until the whole chain has finished.
 */
static void
handle_chain_cancel (IrisProcess *process,
                     IrisMessage *in_message)
{
	IrisMessage *out_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (in_message != NULL);

	g_return_if_fail (iris_process_has_predecessor (process));

	if (FLAG_IS_OFF (process, IRIS_TASK_FLAG_WORK_ACTIVE)) {
		out_message = iris_message_new (IRIS_TASK_MESSAGE_FINISH_CANCEL);
		iris_port_post (IRIS_TASK (process)->priv->port, out_message);
	}
}

static void
handle_add_watch (IrisProcess *process,
                  IrisMessage *message)
{
	IrisProcessPrivate *priv;
	const GValue       *data;
	IrisPort           *watch_port;
	IrisMessage        *progress_message;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	priv = process->priv;

	data = iris_message_get_data (message);
	watch_port = IRIS_PORT (g_value_get_object (data));

	priv->watch_port_list = g_list_append (priv->watch_port_list,
	                                       watch_port);

	/* Send the title. The progress monitor does call iris_process_get_title(),
	 * but it could have changed between iris_progress_monitor_watch_process
	 * and us receiving the ADD_WATCH message
	 */
	progress_message = iris_message_new_data (IRIS_PROGRESS_MESSAGE_TITLE,
	                                          G_TYPE_STRING,
	                                          priv->title);
	post_progress_message (process, progress_message);

	/* Send a status message now, it's possible that the process has actually
	 * already completed and so this may be only status message that the watcher
	 * receives. Force==TRUE because total_items may have been lost in the same
	 * way as title mentioned above.
	 */
	update_status (process, TRUE);

	if (iris_process_is_finished (process)) {
		progress_message = iris_message_new (IRIS_PROGRESS_MESSAGE_COMPLETE);
		post_progress_message (process, progress_message);
	}
}

static void
handle_chain_estimate (IrisProcess *process,
                       IrisMessage *message)
{
	IrisProcessPrivate *priv;
	IrisProcess        *source;
	const GValue       *value;
	gint                source_estimated_total_items;

	g_return_if_fail (IRIS_IS_PROCESS (process));
	g_return_if_fail (message != NULL);

	priv = process->priv;

	source = iris_process_get_predecessor (process);

	if (!source)
		return;

	value = iris_message_get_data (message);
	source_estimated_total_items = g_value_get_int (value);

	if (source_estimated_total_items <= g_atomic_int_get (&priv->estimated_total_items))
		return;

	g_atomic_int_set (&priv->estimated_total_items, source_estimated_total_items);

	post_output_estimate (process);
}

#ifdef IRIS_TRACE_TASK
static const char *process_message_name[] =
  { "no-more-work",
    "add-source",
    "add-sink",
    "chain-cancel",
    "add-watch",
    "chain-estimate"
  };
#endif

static void
iris_process_handle_message_real (IrisTask    *task,
                                  IrisMessage *message)
{
	IrisProcessPrivate *priv;
	IrisProcess *process = IRIS_PROCESS (task);

	g_return_if_fail (IRIS_IS_PROCESS (process));

	priv = process->priv;

	#ifdef IRIS_TRACE_TASK
	if (message->what >= IRIS_PROCESS_MESSAGE_NO_MORE_WORK &&
	    message->what <= IRIS_PROCESS_MESSAGE_CHAIN_ESTIMATE)
		g_print ("process %lx: got message %s\n",
		         (gulong)process, process_message_name [message->what - IRIS_PROCESS_MESSAGE_NO_MORE_WORK]);
	#endif

	switch (message->what) {
	case IRIS_TASK_MESSAGE_START_WORK:
		handle_start_work (process, message);
		break;

	case IRIS_TASK_MESSAGE_CALLBACKS_FINISHED:
		handle_callbacks_finished (process, message);
		break;

	case IRIS_TASK_MESSAGE_START_CANCEL:
		handle_start_cancel (process, message);
		break;

	case IRIS_TASK_MESSAGE_FINISH_CANCEL:
		handle_finish_cancel (process, message);
		break;

	case IRIS_TASK_MESSAGE_DEP_FINISHED:
		handle_dep_finished (process, message);
		break;

	case IRIS_TASK_MESSAGE_DEP_CANCELED:
		handle_dep_canceled (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_NO_MORE_WORK:
		handle_no_more_work (process, message);
		break;

	/* FIXME: watch add_dependency and check if we don't have the dep task
	 * already as a source/sink */

	case IRIS_PROCESS_MESSAGE_ADD_SOURCE:
		handle_add_source (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_ADD_SINK:
		handle_add_sink (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_CHAIN_CANCEL:
		handle_chain_cancel (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_ADD_WATCH:
		handle_add_watch (process, message);
		break;

	case IRIS_PROCESS_MESSAGE_CHAIN_ESTIMATE:
		handle_chain_estimate (process, message);
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

	if (! iris_process_was_canceled (process)) {
		iris_message_ref (work_item);
		iris_queue_push (priv->work_queue, work_item);
	}

	/* total_items and estimated_total_items are updated in iris_process_enqueue() */
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

static gboolean
work_function_can_finish (IrisProcess *process)
{
	if (FLAG_IS_OFF (process, IRIS_PROCESS_FLAG_NO_MORE_WORK))
		return FALSE;

	/* No races possible: only recursive work items can post more work if
	 * NO_MORE_WORK is set, and they must do so before that item completes.
	 * Therefore 'processed' can never reach 'total' before the last work
	 * item completes.
	 */
	if (iris_process_get_queue_length (process) > 0)
		return FALSE;

	return TRUE;
}


static void
iris_process_execute_real (IrisTask *task)
{
	GValue    params[2] = { {0,}, {0,} };
	gboolean  canceled,
	          send_finish_cancel;
	GTimer   *timer;
	IrisProcess        *process;
	IrisProcessPrivate *priv;
	IrisScheduler      *work_scheduler;
	IrisMessage        *message;

	g_return_if_fail (IRIS_IS_PROCESS (task));

	process = IRIS_PROCESS (task);
	priv = process->priv;

	g_value_init (&params[0], G_TYPE_OBJECT);
	g_value_set_object (&params[0], process);
	g_value_init (&params[1], G_TYPE_POINTER);

	g_warn_if_fail (task->priv->closure != NULL);

	/* See TODO about why this code is really dumb and how it could be improved */

	timer = g_timer_new ();

	while (1) {
		IrisMessage *work_item;

		canceled = iris_process_was_canceled (process);

		/* Update progress monitors, no more than four times a second */
		if (priv->watch_port_list != NULL &&
		    g_timer_elapsed (priv->watch_timer, NULL) >= 0.200) {
			g_timer_reset (priv->watch_timer);
			update_status (process, FALSE);
		}

		if (canceled)
			break;

		if (G_UNLIKELY (g_timer_elapsed(timer, NULL) > 1.0))
			goto _yield;

		work_item = iris_queue_try_pop (priv->work_queue);

		if (!work_item) {
			if (work_function_can_finish (process))
				break;

_yield:
			g_value_unset (&params[0]);
			g_timer_destroy (timer);

			/* Yield, by reposting this function to the scheduler and returning.
			 * FIXME: would be nice if we could tell the scheduler "don't execute this for at least
			 * 20ms" or something so we don't waste quite as much power waiting for work.
			 */
			/* Also, we currently give this a ref on the process so that it
			 * won't be destroyed if there's still an execution cycle queued. A
			 * slightly more efficient way would be to remove the cycle from the
			 * scheduler's queue, perhaps.
			 */
			work_scheduler = g_atomic_pointer_get (&IRIS_TASK (process)->priv->work_scheduler);
			iris_scheduler_queue (work_scheduler,
			                      (IrisCallback)iris_process_execute_real,
			                      g_object_ref (process),
			                      g_object_unref);
			return;
		}

		/* Execute work item */
		g_value_set_pointer (&params[1], work_item);
		g_closure_invoke (task->priv->closure, NULL, 2, params, NULL);

		iris_message_unref (work_item);

		g_atomic_int_inc (&priv->processed_items);
	};

	g_value_unset (&params[0]);
	g_timer_destroy (timer);

	if (priv->watch_port_list != NULL)
		update_status (process, TRUE);

	if (iris_task_was_canceled (task)) {
		DISABLE_FLAG (task, IRIS_TASK_FLAG_WORK_ACTIVE);

		if (priv->watch_port_list != NULL) {
			/* Send to watchers now no more progress messages can be sent */
			message = iris_message_new (IRIS_PROGRESS_MESSAGE_CANCELED);
			post_progress_message (process, message);
		}

		/* FINISH_CANCEL is sent on no-more-work or chain-cancel. If those
		 * already happened we need to send it ourselves
		 */
		send_finish_cancel = FALSE;
		if (iris_process_has_predecessor (process)) {
			if (iris_process_is_finished (priv->source))
				send_finish_cancel = TRUE;
		} else
		if (FLAG_IS_ON (process, IRIS_PROCESS_FLAG_NO_MORE_WORK))
			send_finish_cancel = TRUE;

		if (send_finish_cancel) {
			message = iris_message_new (IRIS_TASK_MESSAGE_FINISH_CANCEL);
			iris_port_post (IRIS_TASK (process)->priv->port, message);
		}
	} else {
		/* Execute callbacks, mark finished and unref. */
		iris_task_work_finished (IRIS_TASK (process));

		/* IRIS_PROGRESS_MESSAGE_COMPLETE will be sent when
		 * IRIS_TASK_MESSAGE_CALLBACKS_FINISHED is received
		 */
	}
}

static void
iris_process_finalize (GObject *object)
{
	IrisProcess        *process = IRIS_PROCESS (object);
	IrisProcessPrivate *priv    = process->priv;
	GList              *node;

	if (priv->work_port != NULL) {
		iris_receiver_destroy (priv->work_receiver, FALSE, NULL, FALSE);
		g_object_unref (priv->work_port);

		priv->work_receiver = NULL;
		priv->work_port = NULL;
	}

	#ifdef IRIS_TRACE_TASK
	g_print ("process %lx: finalize()\n", (gulong)process);
	#endif

	/* Work items left over from a cancel should have been freed in cancel() */
	g_warn_if_fail (iris_queue_get_length (priv->work_queue) == 0);
	g_object_unref (priv->work_queue);

	if (priv->source != NULL)
		g_object_unref (priv->source);

	g_slice_free (gfloat, (gfloat *)priv->output_estimate_factor);

	g_free ((gpointer)priv->title);

	for (node=priv->watch_port_list; node; node=node->next)
		g_object_unref (IRIS_PORT (node->data));
	g_list_free (priv->watch_port_list);

	g_timer_destroy (priv->watch_timer);

	G_OBJECT_CLASS (iris_process_parent_class)->finalize (object);
}

static void
iris_process_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	IrisProcess *process;

	process = IRIS_PROCESS (object);

	switch (prop_id) {
		case PROP_TITLE:
			iris_process_set_title (process, g_value_get_string (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
iris_process_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	IrisProcess *process;

	process = IRIS_PROCESS (object);

	switch (prop_id) {
		case PROP_TITLE:
			g_value_set_string (value, iris_process_get_title (process));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
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
	object_class->set_property = iris_process_set_property;
	object_class->get_property = iris_process_get_property;

	/**
	 * IrisProcess:title:
	 *
	 * Title of the process, which may be displayed by #IrisProgressMonitor classes.
	 */
	g_object_class_install_property (object_class,
	                                 PROP_TITLE,
	                                 g_param_spec_string ("title",
	                                                      "Title",
	                                                      "Process title",
	                                                      NULL,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_STATIC_STRINGS));

	g_type_class_add_private (object_class, sizeof(IrisProcessPrivate));
}

static void
iris_process_init (IrisProcess *process)
{
	IrisProcessPrivate *priv;
	float              *p_output_estimation_factor;

	priv = process->priv = IRIS_PROCESS_GET_PRIVATE (process);

	/* FIXME: Leaked? "We should have a teardown port for dispose" */

	/* Default scheduler is used to pass work items, the only real reason is
	 * maybe one work item will take a lot longer than the others, and if that
	 * item blocks the quicker ones being received it prevents them being
	 * processed by other cores ...
	 * FIXME: not sure even this is a compelling rationale ..
	 */
	priv->work_port = iris_port_new ();
	priv->work_receiver = iris_arbiter_receive (iris_get_default_control_scheduler (),
	                                            priv->work_port,
	                                            iris_process_post_work_item,
	                                            process, NULL);
	iris_arbiter_coordinate (priv->work_receiver, NULL, NULL);

	priv->work_queue = iris_queue_new ();

	priv->source = NULL;
	priv->sink = NULL;

	priv->processed_items = 0;
	priv->total_items = 0;
	priv->estimated_total_items = 0;

	priv->watch_total_items = 0;

	/* No atomic float access :( */
	p_output_estimation_factor = g_slice_new (float);
	*p_output_estimation_factor = 1.0;
	priv->output_estimate_factor = p_output_estimation_factor;

	priv->title = NULL;

	priv->watch_port_list = NULL;
	priv->watch_timer = g_timer_new ();

	iris_task_set_progress_mode (IRIS_TASK (process), IRIS_PROGRESS_DISCRETE);
}

static void
iris_process_dummy (IrisProcess *process, IrisMessage *work_item, gpointer user_data)
{
}
