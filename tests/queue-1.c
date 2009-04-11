#include <iris/iris.h>

static void
test1 (void)
{
	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	g_assert (queue->head != NULL);
	g_assert (queue->head->next == NULL);
}

static void
test2 (void)
{
	IrisQueue *queue = iris_queue_new ();
	g_assert (iris_queue_dequeue (queue) == NULL);
}

static void
test3 (void)
{
	gint i;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	iris_queue_enqueue (queue, &i);
	g_assert (iris_queue_dequeue (queue) == &i);
}

static void
test4 (void)
{
	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	g_assert (queue->head != NULL);
	g_assert (queue->head->next == NULL);
	iris_queue_unref (queue);
}

static void
test5 (void)
{
	gint i;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	iris_queue_enqueue (queue, &i);
	g_assert (iris_queue_dequeue (queue) == &i);
	g_assert (iris_queue_dequeue (queue) == NULL);
}

static void
test6 (void)
{
	gint i;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	iris_queue_enqueue (queue, &i);
	g_assert (iris_queue_get_length (queue) == 1);
	g_assert (iris_queue_dequeue (queue) == &i);
	g_assert (iris_queue_get_length (queue) == 0);
	g_assert (iris_queue_dequeue (queue) == NULL);
	g_assert (iris_queue_get_length (queue) == 0);
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
	IrisQueue *queue = iris_queue_new ();
	queue->vtable->destroy = test7_cb;
	iris_queue_unref (queue);
	g_assert (test7_data == TRUE);
}

static void
test8 (void)
{
	IrisQueue *queue = iris_queue_new ();
	queue->vtable->destroy = test7_cb;
	iris_queue_ref (queue);
	iris_queue_unref (queue);
	iris_queue_unref (queue);
	g_assert (test7_data == TRUE);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/queue/new", test1);
	g_test_add_func ("/queue/dequeue_empty", test2);
	g_test_add_func ("/queue/enqueue_dequeue", test3);
	g_test_add_func ("/queue/free", test4);
	g_test_add_func ("/queue/enqueue_dequeue_empty", test5);
	g_test_add_func ("/queue/get_length", test6);
	g_test_add_func ("/queue/unref1", test7);
	g_test_add_func ("/queue/unref2", test8);

	return g_test_run ();
}
