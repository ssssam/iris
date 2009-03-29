#include <iris/iris.h>
#include <string.h>

static void
get_type1 (void)
{
	g_assert (IRIS_TYPE_RECEIVER != G_TYPE_INVALID);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/port/get_type1", get_type1);

	return g_test_run ();
}
