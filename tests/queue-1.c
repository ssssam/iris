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
	iris_queue_free (queue);
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
	g_test_add_func ("/queue/enqueue_dequeue_empty", test5);
	g_test_add_func ("/queue/free", test4);

	return g_test_run ();
}
