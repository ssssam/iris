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
	g_assert (iris_task_get_error (task) == NULL);
	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	g_assert (iris_task_get_error (task) != NULL);
}

static void
test7 (void)
{
	IrisTask *task = iris_task_new (NULL, NULL, NULL);
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
	g_assert (iris_task_get_error (task) == NULL);

	IRIS_TASK_THROW_NEW (task, 1, 1, "Some message here");
	g_assert (iris_task_get_error (task) != NULL);

	IRIS_TASK_CATCH (task, &e);
	g_assert (iris_task_get_error (task) == NULL);
	g_assert (e != NULL);

	IRIS_TASK_THROW (task, e);
	g_assert (iris_task_get_error (task) == e);
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

	return g_test_run ();
}
