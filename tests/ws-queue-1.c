#include <iris/iris.h>

static void
test1 (void)
{
	IrisQueue  *queue;
	IrisQueue  *global;

	global = iris_queue_new ();
	queue = iris_wsqueue_new (global, NULL);
	g_assert (queue);
}

static gboolean test2_data = FALSE;

static void
test2_cb (IrisQueue *queue)
{
	test2_data = TRUE;
}

static void
test2 (void)
{
	IrisQueue  *queue;
	IrisQueue  *global;

	global = iris_queue_new ();
	queue = iris_wsqueue_new (global, NULL);
	queue->vtable->dispose = test2_cb;
	g_assert (queue);
	iris_queue_unref (queue);
	g_assert (test2_data);
}

static void
test3 (void)
{
	IrisQueue  *queue;
	IrisQueue  *global;

	global = iris_queue_new ();
	queue = iris_wsqueue_new (global, NULL);
	queue->vtable->dispose = test2_cb;
	g_assert (queue);
	iris_queue_ref (queue);
	iris_queue_unref (queue);
	iris_queue_unref (queue);
	g_assert (test2_data);
}

static void
test4 (void)
{
	IrisQueue *queue;
	gint i = 0;

	queue = iris_wsqueue_new (NULL, NULL);
	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), &i);
	g_assert (iris_wsqueue_local_pop (IRIS_WSQUEUE (queue)) == &i);
}

static void
test5 (void)
{
	IrisQueue *queue;
	gint i = 0;

	queue = iris_wsqueue_new (NULL, NULL);
	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), &i);
	g_assert (iris_wsqueue_try_steal (IRIS_WSQUEUE (queue), 0) == &i);
}

static void
test6 (void)
{
	IrisQueue  *queue;
	IrisQueue  *global;
	IrisQueue  *neighbor;
	IrisRRobin *rrobin;

	global = iris_queue_new ();
	rrobin = iris_rrobin_new (2);
	neighbor = iris_wsqueue_new (global, rrobin);
	queue = iris_wsqueue_new (global, rrobin);

	iris_rrobin_append (rrobin, queue);
	iris_rrobin_append (rrobin, neighbor);

	iris_wsqueue_local_push (((IrisWSQueue*)queue), GINT_TO_POINTER (1));
	iris_queue_push (global, GINT_TO_POINTER (2));
	iris_wsqueue_local_push (((IrisWSQueue*)neighbor), GINT_TO_POINTER (3));

	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_pop (queue)), ==, 1);
	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_pop (queue)), ==, 2);
	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_pop (queue)), ==, 3);
}

static void
test7 (void)
{
	IrisQueue *queue;

	queue = iris_wsqueue_new (NULL, NULL);
	g_assert (queue);

	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), GINT_TO_POINTER (1));

	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_try_pop (queue)), ==, 1);
}

static void
test8 (void)
{
	IrisQueue *queue;
	GTimeVal   tv = {0,0};

	queue = iris_wsqueue_new (NULL, NULL);
	g_assert (queue);

	iris_wsqueue_local_push (IRIS_WSQUEUE (queue), GINT_TO_POINTER (1));

	g_get_current_time (&tv);
	g_assert_cmpint (GPOINTER_TO_INT (iris_queue_timed_pop (queue, &tv)), ==, 1);
}

static void
test9 (void)
{
	IrisQueue *queue;
	gint i;

	queue = iris_wsqueue_new (NULL, NULL);
	g_assert (queue);

	for (i = 0; i < 50; i++) {
		iris_wsqueue_local_push (IRIS_WSQUEUE (queue), &i);
	}
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/wsqueue/new", test1);
	g_test_add_func ("/wsqueue/free", test2);
	g_test_add_func ("/wsqueue/free2", test3);
	g_test_add_func ("/wsqueue/local1", test4);
	g_test_add_func ("/wsqueue/try_steal1", test5);
	g_test_add_func ("/wsqueue/pop_order1", test6);
	g_test_add_func ("/wsqueue/try_pop1", test7);
	g_test_add_func ("/wsqueue/timed_pop1", test8);
	g_test_add_func ("/wsqueue/many_push1", test9);

	return g_test_run ();
}
