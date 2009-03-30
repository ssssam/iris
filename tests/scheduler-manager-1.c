#include <iris/iris.h>
#include <string.h>

static GMainLoop *main_loop = NULL;

static gboolean
dummy (gpointer data)
{
	return FALSE;
}

static void
main_context1_cb (void)
{
	g_main_loop_quit (main_loop);
}

static void
main_context1 (void)
{
	main_loop = g_main_loop_new (NULL, FALSE);
	iris_scheduler_manager_init (NULL, TRUE, G_CALLBACK (main_context1_cb));

	/* simulate main loop activity in 10ms, this way we know
	 * an iteration through the main loop occurs and our dispatcher
	 * for thread re-balancing can run.
	 */
	g_timeout_add (10, (GSourceFunc)dummy, NULL);

	g_main_loop_run (main_loop);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/scheduler-manager/main_context1", main_context1);

	return g_test_run ();
}
