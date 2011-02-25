#include <iris.h>

static void
destroy_notify_test_cb (gpointer data)
{
	gboolean *p_flag = data;

	g_assert (p_flag != NULL);
	g_assert (*p_flag == FALSE);

	*p_flag = TRUE;
}

static void
test_instance (void)
{
	GValue value = { 0 };
	g_value_init (&value, G_TYPE_DESTRUCTIBLE_POINTER);
}

static void
test_normal (void)
{
	GValue   value = { 0 };
	gboolean flag;

	g_value_init (&value, G_TYPE_DESTRUCTIBLE_POINTER);

	flag = FALSE;
	g_value_set_destructible_pointer (&value, &flag, destroy_notify_test_cb);

	g_assert_cmphex ((gulong)g_value_get_destructible_pointer (&value), ==, (gulong)&flag);

	g_value_unset (&value);
	g_assert (flag == TRUE);
}


int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/gdestructiblepointer/instance", test_instance);
	g_test_add_func ("/gdestructiblepointer/normal", test_normal);

	return g_test_run ();
}
