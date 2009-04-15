#include <iris/iris.h>
#include <string.h>

static void
test1 (void)
{
	g_assert (IRIS_IS_SCHEDULER (iris_scheduler_default ()));
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/scheduler/default1", test1);

	return g_test_run ();
}
