#include <iris.h>
#include <string.h>

static void
test1 (void)
{
	g_assert (IRIS_IS_SCHEDULER (iris_scheduler_default ()));
}

static void
test2 (void)
{
	IrisScheduler* sched = iris_scheduler_default ();
	g_assert_cmpint (iris_scheduler_get_min_threads (sched), ==, 1);
}

static void
test3 (void)
{
	IrisScheduler* sched = iris_scheduler_default ();
	g_assert_cmpint (iris_scheduler_get_max_threads (sched), >=, 2);
}

/* Callback to register that each work item as executed */
#define WORK_COUNT 128
gint    exec_flag[WORK_COUNT],
        counter;

static void
work_register_cb (gpointer data)
{
	gint n = GPOINTER_TO_INT (data);
	exec_flag[n] = TRUE;
	g_atomic_int_inc (&counter);
	g_usleep (500);
}

/* queue: test all work queued items execute */
static void
test_queue (void)
{
	IrisScheduler *scheduler;
	gint           n_threads,
	               i;
	GList         *missing_list;

	for (n_threads=1; n_threads<=8; n_threads++) {
		counter = 0;
		memset (exec_flag, 0, WORK_COUNT * sizeof(gint));

		scheduler = iris_scheduler_new_full (n_threads, n_threads);

		for (i=0; i<WORK_COUNT; i++)
			iris_scheduler_queue (scheduler, work_register_cb, GINT_TO_POINTER (i), NULL);

		while (1) {
			g_usleep (50000);
			missing_list = NULL;

			for (i=0; i<WORK_COUNT; i++)
				if (!exec_flag[i])
					missing_list = g_list_prepend (missing_list, GINT_TO_POINTER(i));

			if (missing_list == NULL && g_atomic_int_get (&counter) == WORK_COUNT)
				break;

			/*g_print ("Missing work: ");
			GList *node;
			for (node=missing_list; node; node=node->next)
				g_print ("%i ", GPOINTER_TO_INT (node->data));
			g_print ("\n");*/

			g_list_free (missing_list);
		}

		g_object_unref (scheduler);
	}
}

/* finalize: test threads are released to the scheduler */
static void
test_finalize (void)
{
	IrisScheduler *scheduler;
	int            i, n_threads,
	               spare_threads;

	for (n_threads=1; n_threads<20; n_threads++) {
		scheduler = iris_scheduler_new_full (n_threads, n_threads);

		for (i=0; i<WORK_COUNT; i++)
			iris_scheduler_queue (scheduler, (IrisCallback)g_usleep, GINT_TO_POINTER (500), NULL);

		spare_threads = iris_scheduler_manager_get_spare_thread_count ();

		g_object_unref (scheduler);

		g_assert_cmpint (iris_scheduler_manager_get_spare_thread_count (), ==,
		                 spare_threads + n_threads);
	}
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/scheduler/default1", test1);
	g_test_add_func ("/scheduler/get_min_threads1", test2);
	g_test_add_func ("/scheduler/get_max_threads1", test3);

	g_test_add_func ("/scheduler/queue()", test_queue);

	g_test_add_func ("/scheduler/finalize", test_finalize);

	return g_test_run ();
}
