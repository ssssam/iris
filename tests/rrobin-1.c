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

static gboolean
apply_1_cb (gpointer    data,
          gpointer    user_data)
{
	gboolean *p = data;
	*p = TRUE;
	return TRUE;
}

static void
test_apply_1 (void)
{
	gboolean a = FALSE, b = FALSE, c = FALSE;
	IrisRRobin *rrobin = iris_rrobin_new (3);

	iris_rrobin_append (rrobin, &a);
	iris_rrobin_append (rrobin, &b);
	iris_rrobin_append (rrobin, &c);

	iris_rrobin_apply (rrobin, apply_1_cb, NULL);
	g_assert (a == TRUE && b == FALSE && c == FALSE);
	iris_rrobin_apply (rrobin, apply_1_cb, NULL);
	g_assert (a == TRUE && b == TRUE && c == FALSE);
	iris_rrobin_apply (rrobin, apply_1_cb, NULL);
	g_assert (a == TRUE && b == TRUE && c == TRUE);

	iris_rrobin_unref (rrobin);
}

/* apply 2: 'apply' callback rejects every item, make sure rrobin does not get
 *          stuck in an infinite loop
 */
static gboolean
apply_2_cb (gpointer    data,
          gpointer    user_data)
{
	return FALSE;
}

static void
test_apply_2 (void)
{
	gboolean a = FALSE, b = FALSE, c = FALSE;
	IrisRRobin *rrobin = iris_rrobin_new (3);

	iris_rrobin_append (rrobin, &a);
	iris_rrobin_append (rrobin, &b);
	iris_rrobin_append (rrobin, &c);

	iris_rrobin_apply (rrobin, apply_2_cb, NULL);
	iris_rrobin_apply (rrobin, apply_2_cb, NULL);
	iris_rrobin_apply (rrobin, apply_2_cb, NULL);

	g_assert (a == FALSE && b == FALSE && c == FALSE);

	iris_rrobin_unref (rrobin);
}

/* apply 3: 'apply' callback rejects all but last item, make sure rrobin
 *          doesn't give up early
 */
static gboolean
apply_3_cb (gpointer    data,
            gpointer    user_data)
{
	gint *counter = data;
	return (++ (*counter)) == 3;
}

static void
test_apply_3 (void)
{
	gint counter = 0;
	IrisRRobin *rrobin = iris_rrobin_new (3);

	iris_rrobin_append (rrobin, &counter);
	iris_rrobin_append (rrobin, &counter);
	iris_rrobin_append (rrobin, &counter);

	iris_rrobin_apply (rrobin, apply_3_cb, NULL);

	g_assert_cmpint (counter, ==, 3);

	iris_rrobin_unref (rrobin);
}

static void
test_remove (void)
{
	gint data = 0;
	IrisRRobin *rrobin = iris_rrobin_new (3);

	iris_rrobin_append (rrobin, &data);
	iris_rrobin_append (rrobin, &data);

	iris_rrobin_remove (rrobin, &data);
	g_assert (rrobin->count == 1);

	iris_rrobin_remove (rrobin, &data);
	g_assert (rrobin->count == 0);

	iris_rrobin_remove (rrobin, &data);
	g_assert (rrobin->count == 0);

	iris_rrobin_unref (rrobin);
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

	g_test_add_func ("/rrobin/apply 1", test_apply_1);
	g_test_add_func ("/rrobin/apply 2", test_apply_2);
	g_test_add_func ("/rrobin/apply 3", test_apply_3);

	g_test_add_func ("/rrobin/remove", test_remove);

	return g_test_run ();
}
