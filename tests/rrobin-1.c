#include <iris/iris.h>

static void
test1 (void)
{
	g_assert_cmpint (IRIS_TYPE_RROBIN, !=, G_TYPE_INVALID);
}

static void
test2 (void)
{
	IrisRRobin *rrobin = iris_rrobin_new (3);
	g_assert_cmpint (rrobin->size, ==, 3);
}

static void
test3 (void)
{
	IrisRRobin *rrobin = iris_rrobin_new (3);
	iris_rrobin_ref (rrobin);
	iris_rrobin_unref (rrobin);
	iris_rrobin_unref (rrobin);
}

static gboolean
test4_cb (IrisRRobin *rrobin,
          gpointer    data,
          gpointer    user_data)
{
	gboolean *p = data;
	*p = TRUE;
	return TRUE;
}

static void
test4 (void)
{
	gboolean a = FALSE, b = FALSE, c = FALSE;
	IrisRRobin *rrobin = iris_rrobin_new (3);
	iris_rrobin_append (rrobin, &a);
	iris_rrobin_append (rrobin, &b);
	iris_rrobin_append (rrobin, &c);
	iris_rrobin_foreach (rrobin, test4_cb, NULL);
	g_assert (a == TRUE);
	g_assert (b == TRUE);
	g_assert (c == TRUE);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/rrobin/get-type", test1);
	g_test_add_func ("/rrobin/verify-size", test2);
	g_test_add_func ("/rrobin/ref-unref", test3);
	g_test_add_func ("/rrobin/foreach", test4);

	return g_test_run ();
}
