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
	IrisScheduler *scheduler;
	gboolean       completed = FALSE;

	scheduler = iris_scheduler_new ();
	g_assert (scheduler != NULL);
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
