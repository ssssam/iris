#include <iris/iris.h>
#include <string.h>

static void
test1 (void)
{
	g_assert (IRIS_IS_SCHEDULER (iris_scheduler_default ()));
}

static void
test2 (void)
{
	IrisScheduler* sched = iris_scheduler_default ();
	g_assert_cmpint (iris_scheduler_get_min_threads (sched), ==, 1);
}

static void
test3 (void)
{
	IrisScheduler* sched = iris_scheduler_default ();
	g_assert_cmpint (iris_scheduler_get_max_threads (sched), >=, 2);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/scheduler/default1", test1);
	g_test_add_func ("/scheduler/get_min_threads1", test2);
	g_test_add_func ("/scheduler/get_max_threads1", test3);

	return g_test_run ();
}
