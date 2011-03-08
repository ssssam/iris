#include <iris.h>
#include "mocks/mock-scheduler.h"
#include <iris/iris-task-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-scheduler-private.h>

static IrisScheduler *default_scheduler = NULL;

#define SETUP()                                                 \
	G_STMT_START {                                          \
		default_scheduler = mock_scheduler_new();       \
		iris_set_default_control_scheduler(default_scheduler);  \
		iris_set_default_work_scheduler(default_scheduler);  \
		g_object_unref (default_scheduler);                     \
	} G_STMT_END

static IrisTask *
test_task_new ()
{
	IrisScheduler *scheduler = mock_scheduler_new ();
	IrisTask      *task;

	task = iris_task_new_full (NULL, NULL, NULL, FALSE, scheduler, scheduler, NULL);

	g_object_unref (scheduler);

	return task;
};

static void
wait_task_messages (IrisTask *task)
{
	IrisPort *control_port;

	control_port = task->priv->port;

	while (iris_port_get_queue_length (control_port) > 0 ||
	       g_atomic_int_get (&iris_port_get_receiver (control_port)->priv->active) > 0)
		g_thread_yield ();
}

static void
test_lifecycle (void)
{
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	g_assert (task != NULL);

	/* Preferred way is iris_task_cancel() */
	g_object_ref_sink (task);
	g_object_unref (task);
}

static void
test_set_schedulers (void)
{
	IrisScheduler *scheduler_1 = mock_scheduler_new (),
	              *scheduler_2 = mock_scheduler_new ();

	IrisTask *task = iris_task_new_full (NULL, NULL, NULL,
	                                     FALSE,
	                                     scheduler_1,
	                                     scheduler_2,
	                                     NULL);

	g_object_unref (scheduler_1);
	g_object_unref (scheduler_2);

	g_assert (task != NULL);

	g_assert (task->priv->receiver->priv->scheduler == scheduler_1);
	g_assert (task->priv->work_scheduler == scheduler_2);

	g_object_ref_sink (task);
	g_object_unref (task);

}

/* Cancel before the task was run - the creation phase */
static void
test_cancel_creation (void)
{
	SETUP();
	IrisTask *task = test_task_new();
	g_object_ref (task);

	g_assert (iris_task_was_canceled (task) == FALSE);
	iris_task_cancel (task);
	g_assert (iris_task_was_canceled (task) == TRUE);

	/* Test cancel frees floating reference */
	g_assert (! g_object_is_floating (task));
	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 1);

	/* Should do nothing, but not be an error */
	iris_task_run (task);

	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 1);
	g_object_unref (task);
}



static void
test4 (void)
{
	SETUP();
	GError *error2 = NULL;
	GError *error = g_error_new (1, 1, "Something %s", "blah");
	IrisTask *task = test_task_new ();
	iris_task_take_fatal_error (task, error);
	iris_task_get_fatal_error (task, &error2);
	g_assert (g_error_matches (error2, error->domain, error->code));
	iris_task_take_fatal_error (task, NULL);

	iris_task_cancel (task);
	g_error_free (error2);
}

static void
test5 (void)
{
	SETUP();
	GError *error = g_error_new (1, 1, "Something %s", "blah");
	IrisTask *task = test_task_new ();
	iris_task_set_fatal_error (task, error);
	GError *error2 = NULL;
	iris_task_get_fatal_error (task, &error2);
	g_assert (error2 != NULL && error2 != error);
	g_error_free (error);
	g_error_free (error2);

	iris_task_cancel (task);
}

static void
test6 (void)
{
	SETUP();
	IrisTask *task = test_task_new ();
	GError *error = NULL;
	iris_task_get_fatal_error (task, &error);
	g_assert (error == NULL);
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	iris_task_get_fatal_error (task, &error);
	g_assert (error != NULL);

	iris_task_cancel (task);
	g_error_free (error);
}

static void
test7 (void)
{
	SETUP();
	IrisTask *task = test_task_new ();
	GError *error = NULL;
	iris_task_get_fatal_error (task, &error);
	g_assert (error == NULL);
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	iris_task_get_fatal_error (task, &error);
	g_assert (error != NULL);
	IRIS_TASK_CATCH (task, NULL);
	g_assert_cmpint (iris_task_get_fatal_error (task, &error),==,0);
	g_assert (error == NULL);

	iris_task_cancel (task);
}

static void
test8 (void)
{
	GError *e = NULL;

	SETUP();

	IrisTask *task = test_task_new ();
	GError *error = NULL;
	iris_task_get_fatal_error (task, &error);
	g_assert (error == NULL);

	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	iris_task_get_fatal_error (task, &error);
	g_assert (error != NULL);

	IRIS_TASK_CATCH (task, &e);
	iris_task_get_fatal_error (task, &error);
	g_assert (error == NULL);
	g_assert (e != NULL);

	IRIS_TASK_THROW (task, e);
	iris_task_get_fatal_error (task, &error);
	g_assert (g_error_matches (error, e->domain, e->code));

	g_error_free (error);
	iris_task_cancel (task);
}

static void
test9 (void)
{
	SETUP();
	GValue value = {0,};

	IrisTask *task = test_task_new ();
	iris_task_get_result (task, &value);
	g_assert_cmpint (G_VALUE_TYPE (&value),==,G_TYPE_INVALID);

	IRIS_TASK_RETURN_VALUE (task, G_TYPE_INT, 123);
	iris_task_get_result (task, &value);

	g_assert_cmpint (G_VALUE_TYPE (&value), ==, G_TYPE_INT);
	g_assert_cmpint (g_value_get_int (&value), ==, 123);

	iris_task_cancel (task);
}

static void
test10 (void)
{
	SETUP();
	GValue value = {0,};

	IrisTask *task = test_task_new ();

	iris_task_get_result (task, &value);
	g_assert_cmpint (G_VALUE_TYPE (&value),==,G_TYPE_INVALID);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "This is my string");

	iris_task_set_result (task, &value);
	g_value_unset (&value);
	iris_task_get_result (task, &value);

	g_assert_cmpint (G_VALUE_TYPE (&value),==,G_TYPE_STRING);
	g_assert_cmpstr (g_value_get_string (&value), ==, "This is my string");

	g_value_unset (&value);
	iris_task_cancel (task);
}

static void
test12 (void)
{
	SETUP();
	IrisTask *task = test_task_new ();
	iris_task_set_main_context (task, g_main_context_default ());
	g_assert (iris_task_get_main_context (task) == g_main_context_default ());
	iris_task_cancel (task);
}

static void
test13 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_full (NULL, NULL, NULL, TRUE, NULL, NULL, NULL);
	g_assert (task != NULL);
	g_assert (iris_task_is_async (task) == TRUE);
	iris_task_cancel (task);
}

static void
test14 (void)
{
	IrisTask *task = iris_task_new_full (NULL, NULL, NULL, TRUE, NULL, NULL, g_main_context_default ());
	g_assert (task != NULL);
	g_assert (iris_task_get_main_context (task) == g_main_context_default ());
	iris_task_cancel (task);
}

static void
run_cb (IrisTask *task,
        gpointer  user_data)
{
	gboolean *cb = user_data;
	g_assert (IRIS_IS_TASK (task));
	*cb = TRUE;
}

static void
test_run (void)
{
	SETUP ();
	gboolean success = FALSE;
	IrisTask *task = iris_task_new_with_func (run_cb, &success, NULL);
	g_object_ref (task);

	/* run should complete synchronously because of our scheduler */
	iris_task_run (task);
	g_assert (success == TRUE);

	g_assert (! g_object_is_floating (task));
	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 1);
	g_object_unref (task);
}

static void
run_with_async_result_cb (IrisTask *task,
                          gpointer  user_data)
{
	gint *cb = user_data;
	g_assert (IRIS_IS_TASK (task));
	g_atomic_int_inc (cb);
}

static void
run_with_async_result_notify (GObject      *task,
                              GAsyncResult *res,
                              gpointer      user_data)
{
	gint *cb = user_data;
	g_assert (IRIS_IS_TASK (task));
	g_atomic_int_inc (cb);
}

static void
test_run_with_async_result (void)
{
	gint count = 0;
	IrisScheduler *scheduler = mock_scheduler_new ();

	IrisTask      *task = iris_task_new_full (run_with_async_result_cb,
	                                          &count,
	                                          NULL,
	                                          FALSE,
	                                          scheduler,
	                                          scheduler,
	                                          NULL);

	/* run should complete synchronously because of our scheduler */
	iris_task_run_with_async_result (task, run_with_async_result_notify, &count);
	g_assert_cmpint (count, ==, 2);

	g_object_unref (scheduler);
}

static void
test18_cb (IrisTask *task,
           gpointer  user_data)
{
	g_assert (IRIS_IS_TASK (task));
	*((gboolean*)user_data) = TRUE;
}

static void
test18 (void)
{
	gboolean success = FALSE;
	IrisTask *task = test_task_new ();
	iris_task_add_callback (task, test18_cb, &success, NULL);
	iris_task_run (task);
	g_assert (success == TRUE);
}

static void
test19_cb1 (IrisTask *task,
            gpointer  user_data)
{
	g_assert (IRIS_IS_TASK (task));
	*((gboolean*)user_data) = TRUE;
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some error message");
}

static void
test19_cb2 (IrisTask *task,
            gpointer  user_data)
{
	g_assert (IRIS_IS_TASK (task));
	*((gboolean*)user_data) = TRUE;
	g_assert (task->priv->error != NULL);
	IRIS_TASK_CATCH (task, NULL);
}

static void
test19 (void)
{
	gboolean cb1 = FALSE;
	gboolean cb2 = FALSE;
	IrisTask *task = test_task_new ();
	iris_task_add_callback (task, test19_cb1, &cb1, NULL);
	iris_task_add_errback (task, test19_cb2, &cb2, NULL);
	g_assert_cmpint (g_list_length (task->priv->handlers), ==, 2);

	/* run the task */
	g_object_ref (task);
	iris_task_run (task);

	/* make sure handlers completed */
	g_assert (task->priv->handlers == NULL);

	/* make sure both callbacks were executed */
	g_assert (cb1 == TRUE);
	g_assert (cb2 == TRUE);

	/* make sure cb2 cleared the error */
	g_assert (task->priv->error == NULL);
	g_object_unref (task);
}

static void
test20_cb1 (IrisTask *task,
            gpointer  user_data)
{
	g_assert (IRIS_IS_TASK (task));
	*((gboolean*)user_data) = TRUE;
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some error message");
}

static void
test20_cb2 (IrisTask *task,
            gpointer  user_data)
{
	GError *error = NULL;
	g_assert (IRIS_IS_TASK (task));
	*((gboolean*)user_data) = TRUE;
	g_assert (task->priv->error != NULL);
	IRIS_TASK_CATCH (task, &error);
	IRIS_TASK_THROW (task, error);
}

static void
test20_cb3 (IrisTask *task,
            gpointer  user_data)
{
	*((gboolean*)user_data) = TRUE;
	IRIS_TASK_CATCH (task, NULL);
}

static void
test20_cb4 (IrisTask *task,
            gpointer  user_data)
{
	*((gboolean*)user_data) = TRUE;
}

static void
test20_skip (IrisTask *task,
             gpointer  user_data)
{
	*((gboolean*)user_data) = TRUE;
}

/* callback-errback-2: test throwing errors in callbacks */
static void
test20 (void)
{
	gboolean cb1 = FALSE, cb2 = FALSE, cb3 = FALSE, cb4 = FALSE;
	gboolean skip = FALSE;

	IrisTask *task = test_task_new ();
	g_object_ref (task);

	/* Throw an error */
	iris_task_add_callback (task, test20_cb1, &cb1, NULL);

	/* Catch the error and throw another one, twice */
	iris_task_add_errback (task, test20_cb2, &cb2, NULL);
	iris_task_add_errback (task, test20_cb2, &cb2, NULL);

	/* This should be skipped because an error condition is active */
	iris_task_add_callback (task, test20_skip, &skip, NULL);

	/* Catch the error */
	iris_task_add_errback (task, test20_cb3, &cb3, NULL);

	/* This should be called, because the error is no longer active */
	iris_task_add_callback (task, test20_cb4, &cb4, NULL);

	iris_task_run (task);

	g_assert (cb1 == TRUE);
	g_assert (cb2 == TRUE);
	g_assert (cb3 == TRUE);
	g_assert (cb4 == TRUE);
	g_assert (skip == FALSE);
	g_assert (task->priv->error == NULL);

	g_object_unref (task);
}

/* cancel during execution: test that task object does not get freed before
 *                          work function has returned when task was cancelled.
 */
static void
cancel_test_cb (IrisTask *task,
                gpointer  user_data)
{
	gint *p_wait_state = user_data;

	g_atomic_int_set (p_wait_state, 1);

	while (g_atomic_int_get (p_wait_state) != 2)
		g_thread_yield ();

	/* Check the task didn't get freed while we are still executing */
	g_assert (iris_task_was_canceled (task) == TRUE);
	g_assert_cmpint (G_OBJECT (task)->ref_count, >, 0);

	g_atomic_int_set (p_wait_state, 3);
}

static void
test_cancel_execution (void)
{
	gint wait_state = 0;
	IrisTask *task = iris_task_new_full (cancel_test_cb,
	                                     &wait_state,
	                                     NULL,
	                                     FALSE,
	                                     iris_scheduler_new (),
	                                     iris_scheduler_new (),
	                                     NULL);
	g_object_ref (task);

	iris_task_run (task);

	/* Wait for task function to start */
	while (g_atomic_int_get (&wait_state) != 1)
		g_thread_yield ();

	/* Now we will cancel it */
	iris_task_cancel (task);
	wait_task_messages (task);

	/* 1: task execution ref, 2: gclosure param ref for task func, 3: our ref */
	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 3);

	g_atomic_int_set (&wait_state, 2);

	while (g_atomic_int_get (&wait_state) != 3)
		g_thread_yield ();

	while (G_OBJECT (task)->ref_count > 1)
		g_thread_yield ();
	g_object_unref (task);
}

/* Cancel during callbacks should be ignored; the task already succeeded */
static void
cancel_callbacks_cb_1 (IrisTask *task,
                       gpointer  user_data)
{
	iris_task_cancel (task);
}

static void
cancel_callbacks_cb_2 (IrisTask *task,
                       gpointer  user_data)
{
	gboolean *p_cb_executed = user_data;
	*p_cb_executed = TRUE;
}

static void
test_cancel_callbacks (void)
{
	gboolean cb_2_executed = FALSE;

	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_add_callback (task, cancel_callbacks_cb_1, NULL, NULL);
	iris_task_add_callback (task, cancel_callbacks_cb_2, &cb_2_executed, NULL);

	iris_task_run (task);

	g_assert (cb_2_executed == TRUE);
}

static void
test_cancel_finished (void)
{
	IrisTask *task;

	SETUP ();

	task = iris_task_new_with_func (NULL, NULL, NULL);
	g_object_ref (task);

	iris_task_run (task);
	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 1);
	g_assert (iris_task_is_finished (task) == TRUE);
	g_assert (iris_task_has_succeeded (task) == TRUE);
	g_assert (iris_task_was_canceled (task) == FALSE);

	iris_task_cancel (task);
	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 1);
	g_assert (iris_task_is_finished (task) == TRUE);
	g_assert (iris_task_has_succeeded (task) == TRUE);
	g_assert (iris_task_was_canceled (task) == FALSE);

	g_object_unref (task);
}

static void
test21 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL),
	         *task_after = iris_task_new_with_func (NULL, NULL, NULL);
	g_object_ref (task);
	g_object_ref (task_after);

	iris_task_add_dependency (task_after, task);

	iris_task_run (task_after);
	g_assert ((task_after->priv->flags & IRIS_TASK_FLAG_NEED_EXECUTE) != 0);
	g_assert ((task_after->priv->flags & IRIS_TASK_FLAG_FINISHED) == 0);
	g_assert (!iris_task_is_finished (task_after));

	iris_task_run (task);
	g_assert (iris_task_is_finished (task));
	g_assert (iris_task_is_finished (task_after));

	g_object_unref (task_after);
	g_object_unref (task);
}

static void
test22 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *task2 = iris_task_new_with_func (NULL, NULL, NULL);
	g_object_ref (task);
	g_object_ref (task2);

	iris_task_add_dependency (task2, task);
	iris_task_cancel (task);
	g_assert (iris_task_was_canceled (task));
	g_assert (iris_task_was_canceled (task2));

	g_object_unref (task2);
	g_object_unref (task);
}

static void
test25 (void)
{
	SETUP();

	IrisTask *t1 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t2 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t3 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t4 = iris_task_vall_of (t1, t2, t3, NULL);

	g_object_ref (t4);

	g_assert (g_list_find (t1->priv->observers, t4) != NULL);
	g_assert (g_list_find (t2->priv->observers, t4) != NULL);
	g_assert (g_list_find (t3->priv->observers, t4) != NULL);

	g_assert (t4->priv->dependencies != NULL);
	iris_task_run (t4);
	g_assert (iris_task_is_finished (t4) == FALSE);
	iris_task_run (t1);
	g_assert (iris_task_is_finished (t4) == FALSE);
	iris_task_run (t2);
	g_assert (iris_task_is_finished (t4) == FALSE);
	iris_task_run (t3);
	g_assert (iris_task_is_finished (t4) == TRUE);

	g_object_unref (t4);
}

static void
test_dep_ownership (void)
{
	SETUP();

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL),
	         *task_before = iris_task_new_with_func (NULL, NULL, NULL);
	g_object_ref (task);
	g_object_ref (task_before);

	iris_task_add_dependency (task, task_before);

	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 2);
	g_assert_cmpint (G_OBJECT (task_before)->ref_count, ==, 3);

	iris_task_run (task);
	iris_task_run (task_before);

	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 1);
	g_assert_cmpint (G_OBJECT (task_before)->ref_count, ==, 1);

	g_object_unref (task);
	g_object_unref (task_before);
}

static void
test26 (void)
{
	SETUP();

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *task_after = iris_task_new_with_func (NULL, NULL, NULL);
	g_object_ref (task_after);

	iris_task_add_dependency (task_after, task);
	iris_task_cancel (task);
	g_assert (iris_task_was_canceled (task_after));

	g_assert_cmpint (G_OBJECT (task_after)->ref_count, ==, 1);

	g_object_unref (task_after);
}

static void
test27 (void)
{
	SETUP();

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *task2 = iris_task_new_with_func (NULL, NULL, NULL);
	g_object_ref (task);

	iris_task_add_dependency (task, task2);
	iris_task_cancel (task);
	g_assert (!iris_task_was_canceled (task2));

	g_assert_cmpint (G_OBJECT (task)->ref_count, ==, 1);
	g_assert_cmpint (G_OBJECT (task2)->ref_count, ==, 2);

	g_object_unref (task);

	g_assert_cmpint (G_OBJECT (task2)->ref_count, ==, 1);
	g_object_unref (task2);
}

static void
test28 (void)
{
	IrisTask *t1 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t2 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t3 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t4 = iris_task_vany_of (t1, t2, t3, NULL);

	g_object_ref (t1);
	g_object_ref (t2);
	g_object_ref (t3);
	g_object_ref (t4);

	iris_task_run (t4);
	g_assert (!iris_task_is_finished (t1));
	g_assert (!iris_task_is_finished (t2));
	g_assert (!iris_task_is_finished (t3));
	g_assert (!iris_task_is_finished (t4));
	iris_task_run (t3);
	g_assert (!iris_task_is_finished (t1));
	g_assert (!iris_task_is_finished (t2));
	g_assert (iris_task_is_finished (t3));
	g_assert (iris_task_is_finished (t4));

	iris_task_cancel (t1);
	iris_task_cancel (t2);

	g_assert_cmpint (G_OBJECT (t4)->ref_count, ==, 1);
	g_assert_cmpint (G_OBJECT (t3)->ref_count, ==, 1);
	g_assert_cmpint (G_OBJECT (t2)->ref_count, ==, 1);
	g_assert_cmpint (G_OBJECT (t1)->ref_count, ==, 1);
	g_object_unref (t1);
	g_object_unref (t2);
	g_object_unref (t3);
	g_object_unref (t4);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/task/lifecycle", test_lifecycle);
	g_test_add_func ("/task/set schedulers", test_set_schedulers);
	g_test_add_func ("/task/cancel in creation", test_cancel_creation);
	g_test_add_func ("/task/take_error1", test4);
	g_test_add_func ("/task/set_error1", test5);
	g_test_add_func ("/task/THROW_NEW1", test6);
	g_test_add_func ("/task/CATCH1", test7);
	g_test_add_func ("/task/THROW1", test8);
	g_test_add_func ("/task/RETURN_VALUE1", test9);
	g_test_add_func ("/task/set_result1", test10);
	g_test_add_func ("/task/main_context1", test12);
	g_test_add_func ("/task/new_full-is_async", test13);
	g_test_add_func ("/task/new_full-context", test14);
	g_test_add_func ("/task/run", test_run);
	g_test_add_func ("/task/run_with_async_result()", test_run_with_async_result);
	g_test_add_func ("/task/add_callback1", test18);
	g_test_add_func ("/task/callback-errback1", test19);
	g_test_add_func ("/task/callback-errback2", test20);
	g_test_add_func ("/task/cancel in execution", test_cancel_execution);
	g_test_add_func ("/task/cancel in callbacks", test_cancel_callbacks);
	g_test_add_func ("/task/cancel in finished", test_cancel_finished);
	g_test_add_func ("/task/dep-clean-finish1", test21);
	g_test_add_func ("/task/all_of1", test25);
	g_test_add_func ("/task/dep ownership", test_dep_ownership);
	g_test_add_func ("/task/cancel dependency", test22);
	g_test_add_func ("/task/cancel dependent", test26);
	g_test_add_func ("/task/cancel-dont-affect1", test27);
	g_test_add_func ("/task/any_of1", test28);

	return g_test_run ();
}
