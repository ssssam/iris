#include <iris/iris.h>
#include "mocks/mock-scheduler.h"

static void
test1 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	g_assert (task != NULL);
}

static void
test2 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	g_assert (task != NULL);
	g_object_unref (task);
}

static void
test3 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	g_assert (task != NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_object_unref (task);
}

static void
test4 (void)
{
	GError *error = g_error_new (1, 1, "Something %s", "blah");
	IrisScheduler *sched = mock_scheduler_new ();
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	iris_task_set_scheduler (task, sched);
	iris_task_take_error (task, error);
	g_assert (iris_task_get_error (task) == error);
	iris_task_take_error (task, NULL);
}

static void
test5 (void)
{
	GError *error = g_error_new (1, 1, "Something %s", "blah");
	IrisScheduler *sched = mock_scheduler_new ();
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	iris_task_set_scheduler (task, sched);
	iris_task_set_error (task, error);
	const GError *new_error = iris_task_get_error (task);
	g_assert (new_error != NULL && new_error != error);
	g_error_free (error);
}

static void
test6 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_assert (iris_task_get_error (task) == NULL);
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	g_assert (iris_task_get_error (task) != NULL);
}

static void
test7 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_assert (iris_task_get_error (task) == NULL);
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	g_assert (iris_task_get_error (task) != NULL);
	IRIS_TASK_CATCH (task, NULL);
	g_assert (iris_task_get_error (task) == NULL);
}

static void
test8 (void)
{
	GError *e = NULL;

	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_assert (iris_task_get_error (task) == NULL);

	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	g_assert (iris_task_get_error (task) != NULL);

	IRIS_TASK_CATCH (task, &e);
	g_assert (iris_task_get_error (task) == NULL);
	g_assert (e != NULL);

	IRIS_TASK_THROW (task, e);
	g_assert (iris_task_get_error (task) == e);
}

static void
test9 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_assert (iris_task_get_result (task) != NULL);
	g_assert (G_VALUE_TYPE (iris_task_get_result (task)) == G_TYPE_INVALID);

	IRIS_TASK_RETURN_VALUE (task, G_TYPE_INT, 123);
	g_assert_cmpint (G_VALUE_TYPE (iris_task_get_result (task)), ==, G_TYPE_INT);
	g_assert_cmpint (g_value_get_int (iris_task_get_result (task)), ==, 123);
}

static void
test10 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
	iris_task_set_scheduler (task, mock_scheduler_new ());
	g_assert (iris_task_get_result (task) != NULL);
	g_assert (G_VALUE_TYPE (iris_task_get_result (task)) == G_TYPE_INVALID);

	GValue value = {0,};
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "This is my string");

	iris_task_set_result (task, &value);
	g_assert_cmpint (G_VALUE_TYPE (iris_task_get_result (task)), ==, G_TYPE_STRING);
	g_assert_cmpstr (g_value_get_string (iris_task_get_result (task)), ==, "This is my string");
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

	return g_test_run ();
}
