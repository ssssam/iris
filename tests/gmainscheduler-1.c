#include <iris.h>

static void
test1 (void)
{
	IrisScheduler *scheduler;

	scheduler = iris_gmainscheduler_new (NULL);
	g_assert (scheduler != NULL);

	scheduler = iris_gmainscheduler_new (g_main_context_default ());
	g_assert (scheduler != NULL);
}

static void
test2_cb (gpointer data)
{
	GMainLoop *main_loop = data;
	g_assert (main_loop != NULL);
	g_main_loop_quit (main_loop);
}

static void
test2 (void)
{
	IrisScheduler *scheduler;
	GMainContext  *context;
	GMainLoop     *main_loop;

	context = g_main_context_default ();
	main_loop = g_main_loop_new (context, FALSE);
	scheduler = iris_gmainscheduler_new (context);

	iris_scheduler_queue (scheduler, test2_cb, main_loop, NULL);
	g_main_loop_run (main_loop);
}

gint
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	iris_init ();

	g_test_add_func ("/gmainscheduler/new", test1);
	g_test_add_func ("/gmainscheduler/queue", test2);

	return g_test_run ();
}
