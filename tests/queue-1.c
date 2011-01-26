#include <iris/iris.h>
#include <iris/iris-queue-private.h>

static void
test1 (void)
{
	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	g_assert (queue->priv->q != NULL);
}

static void
test2 (void)
{
	IrisQueue *queue = iris_queue_new ();
	g_assert (iris_queue_try_pop (queue) == NULL);
}

static void
test3 (void)
{
	gint i;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	iris_queue_push (queue, &i);
	g_assert (iris_queue_pop (queue) == &i);
}

static void
test4 (void)
{
	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	g_object_unref (queue);
	/* success if no segfault :-) */
}

static void
test5 (void)
{
	gint i;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	iris_queue_push (queue, &i);
	g_assert (iris_queue_try_pop (queue) == &i);
	g_assert (iris_queue_try_pop (queue) == NULL);
}

static void
test6 (void)
{
	gint i;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	iris_queue_push (queue, &i);
	g_assert (iris_queue_length (queue) == 1);
	g_assert (iris_queue_try_pop (queue) == &i);
	g_assert (iris_queue_length (queue) == 0);
	g_assert (iris_queue_try_pop (queue) == NULL);
	g_assert (iris_queue_length (queue) == 0);
}

static void
test9 (void)
{
	g_assert_cmpint (IRIS_TYPE_QUEUE, !=, G_TYPE_INVALID);
}


static void
test_try_pop_or_close (void)
{
	gint     i;
	gpointer ptr;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	g_assert (iris_queue_is_closed (queue) == FALSE);

	iris_queue_push (queue, &i);
	g_assert (iris_queue_is_closed (queue) == FALSE);

	ptr = iris_queue_try_pop_or_close (queue);
	g_assert (ptr == &i);
	g_assert (iris_queue_is_closed (queue) == FALSE);

	ptr = iris_queue_try_pop_or_close (queue);
	g_assert (ptr == NULL);
	g_assert (iris_queue_is_closed (queue) == TRUE);

	g_object_unref (queue);
}

static void
test_timed_pop_or_close (void)
{
	gint     i;
	gpointer ptr;
	GTimeVal timeout;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	g_assert (iris_queue_is_closed (queue) == FALSE);

	iris_queue_push (queue, &i);
	g_assert (iris_queue_is_closed (queue) == FALSE);

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, 100000);

	ptr = iris_queue_timed_pop_or_close (queue, &timeout);
	g_assert (ptr == &i);
	g_assert (iris_queue_is_closed (queue) == FALSE);

	ptr = iris_queue_timed_pop_or_close (queue, &timeout);
	g_assert (ptr == NULL);
	g_assert (iris_queue_is_closed (queue) == TRUE);

	g_object_unref (queue);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/queue/new", test1);
	g_test_add_func ("/queue/pop_empty", test2);
	g_test_add_func ("/queue/push_pop", test3);
	g_test_add_func ("/queue/free", test4);
	g_test_add_func ("/queue/push_pop_empty", test5);
	g_test_add_func ("/queue/get_length", test6);
	g_test_add_func ("/queue/get_type", test9);

	g_test_add_func ("/queue/try_pop_or_close", test_try_pop_or_close);
	g_test_add_func ("/queue/timed_pop_or_close", test_timed_pop_or_close);

	return g_test_run ();
}
