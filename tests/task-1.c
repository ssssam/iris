#include <iris/iris.h>
#include "mocks/mock-scheduler.h"
#include <iris/iris-task-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-scheduler-private.h>

static IrisScheduler *default_scheduler = NULL;

#define SETUP()                                                 \
	G_STMT_START {                                          \
		default_scheduler = mock_scheduler_new();       \
		iris_scheduler_set_default(default_scheduler);  \
	} G_STMT_END

static void
test1 (void)
{
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	g_assert (task != NULL);
}

static void
test2 (void)
{
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	g_assert (task != NULL);
	g_object_unref (task);
}

static void
test3 (void)
{
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	g_assert (task != NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_object_unref (task);
}

static void
test4 (void)
{
	SETUP();
	GError *error2 = NULL;
	GError *error = g_error_new (1, 1, "Something %s", "blah");
	IrisScheduler *sched = mock_scheduler_new ();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, sched);
	iris_task_take_error (task, error);
	iris_task_get_error (task, &error2);
	g_assert (g_error_matches (error2, error->domain, error->code));
	iris_task_take_error (task, NULL);
}

static void
test5 (void)
{
	SETUP();
	GError *error = g_error_new (1, 1, "Something %s", "blah");
	IrisScheduler *sched = mock_scheduler_new ();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, sched);
	iris_task_set_error (task, error);
	GError *error2 = NULL;
	iris_task_get_error (task, &error2);
	g_assert (error2 != NULL && error2 != error);
	g_error_free (error);
	g_error_free (error2);
}

static void
test6 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	GError *error = NULL;
	iris_task_get_error (task, &error);
	g_assert (error == NULL);
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	iris_task_get_error (task, &error);
	g_assert (error != NULL);
}

static void
test7 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	GError *error = NULL;
	iris_task_get_error (task, &error);
	g_assert (error == NULL);
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	iris_task_get_error (task, &error);
	g_assert (error != NULL);
	IRIS_TASK_CATCH (task, NULL);
	g_assert_cmpint (iris_task_get_error (task, &error),==,0);
	g_assert (error == NULL);
}

static void
test8 (void)
{
	GError *e = NULL;

	SETUP();

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	GError *error = NULL;
	iris_task_get_error (task, &error);
	g_assert (error == NULL);

	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	iris_task_get_error (task, &error);
	g_assert (error != NULL);

	IRIS_TASK_CATCH (task, &e);
	iris_task_get_error (task, &error);
	g_assert (error == NULL);
	g_assert (e != NULL);

	IRIS_TASK_THROW (task, e);
	iris_task_get_error (task, &error);
	g_assert (g_error_matches (error, e->domain, e->code));
}

static void
test9 (void)
{
	SETUP();
	GValue value = {0,};

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	iris_task_get_result (task, &value);
	g_assert_cmpint (G_VALUE_TYPE (&value),==,G_TYPE_INVALID);

	IRIS_TASK_RETURN_VALUE (task, G_TYPE_INT, 123);
	iris_task_get_result (task, &value);

	g_assert_cmpint (G_VALUE_TYPE (&value), ==, G_TYPE_INT);
	g_assert_cmpint (g_value_get_int (&value), ==, 123);
}

static void
test10 (void)
{
	SETUP();
	GValue value = {0,};

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());

	iris_task_get_result (task, &value);
	g_assert_cmpint (G_VALUE_TYPE (&value),==,G_TYPE_INVALID);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "This is my string");

	iris_task_set_result (task, &value);
	g_value_unset (&value);
	iris_task_get_result (task, &value);

	g_assert_cmpint (G_VALUE_TYPE (&value),==,G_TYPE_STRING);
	g_assert_cmpstr (g_value_get_string (&value), ==, "This is my string");
}

static void
test11 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_assert (iris_task_is_canceled (task) == FALSE);
	iris_task_cancel (task);
	g_assert (iris_task_is_canceled (task) == TRUE);
}

static void
test12 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	iris_task_set_main_context (task, g_main_context_default ());
	g_assert (iris_task_get_main_context (task) == g_main_context_default ());
}

static void
test13 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_full (NULL, NULL, NULL, TRUE, NULL, NULL);
	g_assert (task != NULL);
	g_assert (iris_task_is_async (task) == TRUE);
}

static void
test14 (void)
{
	IrisTask *task = iris_task_new_full (NULL, NULL, NULL, TRUE, NULL, g_main_context_default ());
	g_assert (task != NULL);
	g_assert (iris_task_get_main_context (task) == g_main_context_default ());
}

static void
test15 (void)
{
	IrisScheduler *sched = mock_scheduler_new ();
	IrisTask *task = iris_task_new_full (NULL, NULL, NULL, TRUE, sched, NULL);
	g_assert (task != NULL);
	g_assert (task->priv->receiver->priv->scheduler == sched);
}

static void
test16_cb (IrisTask *task,
           gpointer  user_data)
{
	gboolean *cb = user_data;
	g_assert (IRIS_IS_TASK (task));
	*cb = TRUE;
}

static void
test16 (void)
{
	gboolean success = FALSE;
	IrisTask *task = iris_task_new_with_func (test16_cb, &success, NULL);
	/* run should complete synchronously because of our scheduler */
	iris_task_run (task);
	g_assert (success == TRUE);
}

static void
test17_cb (IrisTask *task,
           gpointer  user_data)
{
	gint *cb = user_data;
	g_assert (IRIS_IS_TASK (task));
	g_atomic_int_inc (cb);
}

static void
test17_notify (GObject      *task,
               GAsyncResult *res,
               gpointer      user_data)
{
	gint *cb = user_data;
	g_assert (IRIS_IS_TASK (task));
	g_atomic_int_inc (cb);
}

static void
test17 (void)
{
	gint count = 0;
	IrisTask *task = iris_task_new_with_func (test17_cb, &count, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	/* run should complete synchronously because of our scheduler */
	iris_task_run_async (task, test17_notify, &count);
	g_assert_cmpint (count, ==, 2);
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
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
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
	IrisTask *task = iris_task_new_full (NULL, NULL, NULL, FALSE, mock_scheduler_new (), NULL);
	iris_task_add_callback (task, test19_cb1, &cb1, NULL);
	iris_task_add_errback (task, test19_cb2, &cb2, NULL);
	g_assert_cmpint (g_list_length (task->priv->handlers), ==, 2);

	/* run the task */
	iris_task_run (task);

	/* make sure handlers completed */
	g_assert (task->priv->handlers == NULL);

	/* make sure both callbacks were executed */
	g_assert (cb1 == TRUE);
	g_assert (cb2 == TRUE);

	/* make sure cb2 cleared the error */
	g_assert (task->priv->error == NULL);
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

static void
test20 (void)
{
	gboolean cb1 = FALSE;
	gboolean cb2 = FALSE;
	gboolean cb3 = FALSE;
	gboolean cb4 = FALSE;
	gboolean skip = FALSE;
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	iris_task_add_callback (task, test20_cb1, &cb1, NULL);
	iris_task_add_errback (task, test20_cb2, &cb2, NULL);
	iris_task_add_errback (task, test20_cb2, &cb2, NULL);
	iris_task_add_callback (task, test20_skip, &skip, NULL);
	iris_task_add_errback (task, test20_cb3, &cb3, NULL);
	iris_task_add_callback (task, test20_cb4, &cb4, NULL);
	iris_task_run (task);
	g_assert (cb1 == TRUE);
	g_assert (cb2 == TRUE);
	g_assert (cb3 == TRUE);
	g_assert (cb4 == TRUE);
	g_assert (skip == FALSE);
	g_assert (task->priv->error == NULL);
}

static void
test21 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *task2 = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_add_dependency (task2, task);
	iris_task_run (task2);
	g_assert ((task2->priv->flags & IRIS_TASK_FLAG_NEED_EXECUTE) != 0);
	g_assert ((task2->priv->flags & IRIS_TASK_FLAG_FINISHED) == 0);
	g_assert (!iris_task_is_finished (task2));
	iris_task_run (task);
	g_assert (iris_task_is_finished (task));
	g_assert (iris_task_is_finished (task2));
}

static void
test22 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *task2 = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_add_dependency (task2, task);
	iris_task_cancel (task);
	g_assert (iris_task_is_canceled (task));
	g_assert (iris_task_is_canceled (task2));
}

static void
test23_cb (IrisTask *task,
           gpointer  user_data)
{
	gboolean *s = user_data;
	*s = TRUE;
}

static void
test23 (void)
{
	SETUP();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	g_assert (iris_task_is_finished (task) == FALSE);
	iris_task_run (task);
	g_assert (iris_task_is_finished (task) == TRUE);
	gboolean success = FALSE;
	iris_task_add_callback (task, test23_cb, &success, NULL);
	g_assert (iris_task_is_finished (task) == TRUE);
	g_assert (success == TRUE);
	g_assert (iris_task_is_canceled (task) == FALSE);
}

static void
test24 (void)
{
	IrisScheduler *sched = mock_scheduler_new ();
	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_set_scheduler (task, sched);
	g_assert (iris_task_is_finished (task) == FALSE);
	iris_task_run (task);
	g_assert (iris_task_is_finished (task) == TRUE);
	gboolean success = FALSE;
	iris_task_cancel (task);
	g_assert (iris_task_is_canceled (task) == TRUE);
	iris_task_add_callback (task, test23_cb, &success, NULL);
	g_assert (iris_task_is_finished (task) == TRUE);
	g_assert (success == FALSE);
}

static void
test25 (void)
{
	SETUP();

	IrisTask *t1 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t2 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t3 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t4 = iris_task_vall_of (t1, t2, t3, NULL);

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
}

static void
test26 (void)
{
	SETUP();

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *task2 = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_add_dependency (task2, task);
	iris_task_cancel (task);
	g_assert (iris_task_is_canceled (task2));
}

static void
test27 (void)
{
	SETUP();

	IrisTask *task = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *task2 = iris_task_new_with_func (NULL, NULL, NULL);
	iris_task_add_dependency (task, task2);
	iris_task_cancel (task);
	g_assert (!iris_task_is_canceled (task2));
}

static void
test28 (void)
{
	IrisTask *t1 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t2 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t3 = iris_task_new_with_func (NULL, NULL, NULL);
	IrisTask *t4 = iris_task_vany_of (t1, t2, t3, NULL);
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
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/task/new1", test1);
	g_test_add_func ("/task/unref1", test2);
	g_test_add_func ("/task/set_scheduler1", test3);
	g_test_add_func ("/task/take_error1", test4);
	g_test_add_func ("/task/set_error1", test5);
	g_test_add_func ("/task/THROW_NEW1", test6);
	g_test_add_func ("/task/CATCH1", test7);
	g_test_add_func ("/task/THROW1", test8);
	g_test_add_func ("/task/RETURN_VALUE1", test9);
	g_test_add_func ("/task/set_result1", test10);
	g_test_add_func ("/task/cancel1", test11);
	g_test_add_func ("/task/main_context1", test12);
	g_test_add_func ("/task/new_full-is_async", test13);
	g_test_add_func ("/task/new_full-context", test14);
	g_test_add_func ("/task/new_full-scheduler", test15);
	g_test_add_func ("/task/run", test16);
	g_test_add_func ("/task/run_async", test17);
	g_test_add_func ("/task/add_callback1", test18);
	g_test_add_func ("/task/callback-errback1", test19);
	g_test_add_func ("/task/callback-errback2", test20);
	g_test_add_func ("/task/dep-clean-finish1", test21);
	g_test_add_func ("/task/cancel2", test22);
	g_test_add_func ("/task/callback-after-finish1", test23);
	g_test_add_func ("/task/callback-after-finish-cancel1", test24);
	g_test_add_func ("/task/all_of1", test25);
	g_test_add_func ("/task/dep_canceled", test26);
	g_test_add_func ("/task/cancel-dont-affect1", test27);
	g_test_add_func ("/task/any_of1", test28);

	return g_test_run ();
}
