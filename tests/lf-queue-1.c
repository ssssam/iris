#include <iris/iris.h>

static void
test1 (void)
{
	IrisQueue *queue = iris_lfqueue_new ();
	g_assert (queue != NULL);
	g_assert (((IrisLFQueue*)queue)->head != NULL);
	g_assert (((IrisLFQueue*)queue)->head->next == NULL);
}

static void
test2 (void)
{
	IrisQueue *queue = iris_lfqueue_new ();
	g_assert (iris_queue_try_pop (queue) == NULL);
}

static void
test3 (void)
{
	gint i;

	IrisQueue *queue = iris_lfqueue_new ();
	g_assert (queue != NULL);
	iris_queue_push (queue, &i);
	g_assert (iris_queue_pop (queue) == &i);
}

static void
test4 (void)
{
	IrisQueue *queue = iris_lfqueue_new ();
	g_assert (queue != NULL);
	g_assert (((IrisLFQueue*)queue)->head != NULL);
	g_assert (((IrisLFQueue*)queue)->head->next == NULL);
	iris_queue_unref (queue);
}

static void
test5 (void)
{
	gint i;

	IrisQueue *queue = iris_lfqueue_new ();
	g_assert (queue != NULL);
	iris_queue_push (queue, &i);
	g_assert (iris_queue_pop (queue) == &i);
	g_assert (iris_queue_try_pop (queue) == NULL);
}

static void
test6 (void)
{
	gint i;

	IrisQueue *queue = iris_lfqueue_new ();
	g_assert (queue != NULL);
	iris_queue_push (queue, &i);
	g_assert (iris_queue_length (queue) == 1);
	g_assert (iris_queue_pop (queue) == &i);
	g_assert (iris_queue_length (queue) == 0);
	g_assert (iris_queue_try_pop (queue) == NULL);
	g_assert (iris_queue_length (queue) == 0);
}

static gboolean test7_data = FALSE;

static void
test7_cb (IrisQueue *queue)
{
	test7_data = TRUE;
}

static void
test7 (void)
{
	IrisQueue *queue = iris_lfqueue_new ();
	queue->vtable->dispose = test7_cb;
	iris_queue_unref (queue);
	g_assert (test7_data == TRUE);
}

static void
test8 (void)
{
	IrisQueue *queue = iris_lfqueue_new ();
	queue->vtable->dispose = test7_cb;
	iris_queue_ref (queue);
	iris_queue_unref (queue);
	iris_queue_unref (queue);
	g_assert (test7_data == TRUE);
}

static void
test9 (void)
{
	g_assert_cmpint (IRIS_TYPE_LFQUEUE, !=, G_TYPE_INVALID);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/lfqueue/new", test1);
	g_test_add_func ("/lfqueue/pop_empty", test2);
	g_test_add_func ("/lfqueue/push_pop", test3);
	g_test_add_func ("/lfqueue/free", test4);
	g_test_add_func ("/lfqueue/push_pop_empty", test5);
	g_test_add_func ("/lfqueue/length", test6);
	g_test_add_func ("/lfqueue/unref1", test7);
	g_test_add_func ("/lfqueue/unref2", test8);
	g_test_add_func ("/lfqueue/get_type", test9);

	return g_test_run ();
}
