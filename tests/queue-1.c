#include <iris.h>
#include <iris/iris-queue-private.h>

/* FIXME: most of these tests could cover all three queue types ... */

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
	g_assert (iris_queue_get_length (queue) == 1);
	g_assert (iris_queue_try_pop (queue) == &i);
	g_assert (iris_queue_get_length (queue) == 0);
	g_assert (iris_queue_try_pop (queue) == NULL);
	g_assert (iris_queue_get_length (queue) == 0);
}

static void
test9 (void)
{
	g_assert_cmpint (IRIS_TYPE_QUEUE, !=, G_TYPE_INVALID);
}


static void
test_pop_closed_1 (void)
{
	gboolean result;
	gint     i;
	gpointer ptr;

	IrisQueue *queue = iris_queue_new ();
	g_assert (queue != NULL);
	g_assert (iris_queue_is_closed (queue) == FALSE);

	result = iris_queue_push (queue, &i);
	g_assert (result == TRUE);

	iris_queue_close (queue);
	g_assert (iris_queue_is_closed (queue) == TRUE);

	g_assert_cmpint (iris_queue_get_length (queue), ==, 1);

	result = iris_queue_push (queue, &i);
	g_assert (result == FALSE);

	ptr = iris_queue_pop (queue);
	g_assert (iris_queue_is_closed (queue) == TRUE);
	g_assert (ptr == &i);

	ptr = iris_queue_pop (queue);
	g_assert (iris_queue_is_closed (queue) == TRUE);
	g_assert (ptr == NULL);

	g_object_unref (queue);
}


static void
test_pop_closed_2 (void)
{
	GThread *thread[4];
	GError  *error = NULL;
	gint     i, j,
	         items_received;
	gpointer ptr[8],
	         item;
	gboolean result;

	for (i=0; i<50; i++) {
		IrisQueue *queue = iris_queue_new ();

		for (j=0; j<4; j++) {
			thread[j] = g_thread_create ((GThreadFunc)iris_queue_pop, queue, TRUE, &error);
			g_assert_no_error (error);
		}

		/* This is the value of the close token; we need to make sure it
		 * doesn't get swallowed when it's just an item.
		 */
		item = GINT_TO_POINTER (0x12345678);

		for (j=0; j<2; j++)
			result = iris_queue_push (queue, item);
		g_assert (result == TRUE);

		iris_queue_close (queue);
		g_assert (iris_queue_is_closed (queue) == TRUE);

		/* Check pop results. We should have all the items and the rest NULL. */
		items_received = 0;
		for (j=0; j<4; j++) {
			ptr[j] = g_thread_join (thread[j]);

			if (ptr[j] == item) {
				items_received ++;
			} else
				g_assert (ptr[j] == NULL);
		}

		g_assert_cmpint (items_received, ==, 2);

		g_object_unref (queue);
	}
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

	g_test_add_func ("/queue/pop() closed 1", test_pop_closed_1);
	g_test_add_func ("/queue/pop() closed 2", test_pop_closed_2);
	g_test_add_func ("/queue/try_pop_or_close()", test_try_pop_or_close);
	g_test_add_func ("/queue/timed_pop_or_close()", test_timed_pop_or_close);

	return g_test_run ();
}
