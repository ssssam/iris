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

	return g_test_run ();
}
