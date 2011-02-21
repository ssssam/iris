#include <iris.h>
#include <string.h>

#include "iris/iris-process-private.h"

static void
wait_func (IrisProcess *process,
           IrisMessage *work_item,
           gpointer     user_data)
{
	gint *p_wait_state = user_data;

	if (g_atomic_int_get (p_wait_state)==0)
		g_atomic_int_set (p_wait_state, 1);
	else
		while (g_atomic_int_get (p_wait_state) != 2)
			g_usleep (100000);
}

/* load-spread: make sure all available threads get used! */
static void
test_load_spread ()
{
	IrisScheduler *work_scheduler;
	IrisProcess   *process[4];
	gint           i,
	               wait_state[4];
	gboolean       reached_state;

	work_scheduler = iris_scheduler_new_full (4, 4);
	iris_set_default_work_scheduler (work_scheduler);

	memset (wait_state, 0, 4 * sizeof(int));

	for (i=0; i<4; i++) {
		process[i] = iris_process_new_with_func (wait_func, &wait_state[i], NULL);
		iris_process_enqueue (process[i], iris_message_new (0));
		iris_process_no_more_work (process[i]);
		iris_process_run (process[i]);
	}

	/* This will hang if the scheduler does not queue the work properly, it
	 * will only proceed if each work item was put in a separate thread.
	 */
	do {
		reached_state = TRUE;
		for (i=0; i<4; i++)
			if (!g_atomic_int_get (&wait_state[i]))
				reached_state = FALSE;
		g_thread_yield ();
	} while (!reached_state);

	for (i=0; i<4; i++)
		g_atomic_int_set (&wait_state[i], 2);

	for (i=0; i<4; i++)
		g_object_unref (process[i]);

	g_object_unref (work_scheduler);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/scheduler-process/load spread", test_load_spread);

	return g_test_run ();
}
