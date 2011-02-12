#include <iris.h>

static void
test1 (void)
{
	g_assert_cmpint (IRIS_TYPE_THREAD, !=, G_TYPE_INVALID);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/thread/get-type", test1);

	return g_test_run ();
}
