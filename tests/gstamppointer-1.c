#include <iris.h>
#include <iris/gstamppointer.h>

struct T
{
	gint a;
	gint b;
	gint c;
};

static void
test1 (void)
{
	gstamppointer p = g_slice_new (struct T);
	g_assert (p == G_STAMP_POINTER_GET_POINTER (p));
	g_assert (G_STAMP_POINTER_GET_STAMP (p) == 0);
}

static void
test2 (void)
{
	gstamppointer p = g_slice_new (struct T);
	g_assert (p == G_STAMP_POINTER_GET_POINTER (p));
	g_assert (G_STAMP_POINTER_GET_STAMP (p) == 0);
	gstamppointer p2 = G_STAMP_POINTER_INCREMENT (p);
	g_assert (p == G_STAMP_POINTER_GET_POINTER (p2));
	g_assert (G_STAMP_POINTER_GET_STAMP (p2) == 1);
	p2 = G_STAMP_POINTER_INCREMENT (p2);
	p2 = G_STAMP_POINTER_INCREMENT (p2);
	g_assert_cmpint (G_STAMP_POINTER_GET_STAMP (p2), ==, 3);
}

static void
test3 (void)
{
	gstamppointer p = g_slice_new (struct T);
	p = G_STAMP_POINTER_INCREMENT (p);
	g_assert_cmpint (G_STAMP_POINTER_GET_STAMP (p), ==, 1);
	p = G_STAMP_POINTER_INCREMENT (p);
	g_assert_cmpint (G_STAMP_POINTER_GET_STAMP (p), ==, 2);
	p = G_STAMP_POINTER_INCREMENT (p);
	g_assert_cmpint (G_STAMP_POINTER_GET_STAMP (p), ==, 3);
	p = G_STAMP_POINTER_INCREMENT (p);
	g_assert_cmpint (G_STAMP_POINTER_GET_STAMP (p), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/gstamppointer/get-pointer", test1);
	g_test_add_func ("/gstamppointer/inc-stamp", test2);
	g_test_add_func ("/gstamppointer/stamp-rollober", test3);

	return g_test_run ();
}
