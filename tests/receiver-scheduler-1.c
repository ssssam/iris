#include <iris/iris.h>
#include <string.h>

#include <iris/iris-arbiter-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-scheduler-private.h>

/* Testing if receiver gets messages after its own destruction, because we
 * can't be certain it will cause a segfault.
 */
IrisReceiver *global_receiver;

static void
test_message_handler (IrisMessage *message,
                      gpointer     data)
{
	g_assert (IRIS_IS_RECEIVER (global_receiver));
}


typedef struct {
	GMainLoop           *main_loop;
	IrisScheduler       *scheduler;
} SchedulerFixture;

static void
iris_scheduler_fixture_setup (SchedulerFixture *fixture,
                              gconstpointer     user_data)
{
	fixture->main_loop = NULL;
	fixture->scheduler = iris_scheduler_new ();
}

static void
iris_scheduler_fixture_teardown (SchedulerFixture *fixture,
                                 gconstpointer    user_data)
{
	g_object_unref (fixture->scheduler);
}

static void
iris_gmainscheduler_fixture_setup (SchedulerFixture *fixture,
                                   gconstpointer    user_data)
{
	fixture->main_loop = g_main_loop_new (NULL, TRUE);
	fixture->scheduler = iris_gmainscheduler_new (NULL);
}

static void
iris_gmainscheduler_fixture_teardown (SchedulerFixture *fixture,
                                      gconstpointer    user_data)
{
	g_object_unref (fixture->scheduler);
	g_main_loop_unref (fixture->main_loop);
}

static void
iris_lfscheduler_fixture_setup (SchedulerFixture *fixture,
                                gconstpointer    user_data)
{
	fixture->scheduler = iris_lfscheduler_new ();
}

static void
iris_lfscheduler_fixture_teardown (SchedulerFixture *fixture,
                                   gconstpointer    user_data)
{
	g_object_unref (fixture->scheduler);
}

static void
iris_wsscheduler_fixture_setup (SchedulerFixture *fixture,
                                gconstpointer    user_data)
{
	fixture->scheduler = iris_wsscheduler_new ();
}

static void
iris_wsscheduler_fixture_teardown (SchedulerFixture *fixture,
                                   gconstpointer    user_data)
{
	g_object_unref (fixture->scheduler);
}

static void
destruction1 (SchedulerFixture *fixture,
              gconstpointer     user_data)
{
	IrisMessage  *msg;
	IrisReceiver *r;
	IrisPort     *p;
	gint j;

	/* Try to catch the scheduler out by freeing the receiver while it has
	 * messages queued.
	 */

	p = iris_port_new ();
	r = iris_arbiter_receive (fixture->scheduler, p, test_message_handler,
	                          NULL, NULL);
	global_receiver = r;

	for (j = 0; j < 10; j++) {
		msg = iris_message_new (1);
		iris_port_post (p, msg);
	}

	iris_port_set_receiver (p, NULL);

	g_object_unref (r);

	if (fixture->main_loop != NULL) 
		for (j = 0; j < 50; j++)
			g_main_context_iteration (NULL, FALSE);
	else
		g_usleep (500);

	g_object_unref (p);
}

static void
add_tests_with_fixture (void (*setup) (SchedulerFixture *, gconstpointer),
                        void (*teardown) (SchedulerFixture *, gconstpointer),
                        const char *name)
{
	char buf[256];

	g_snprintf (buf, 255, "/receiver-scheduler/%s/destruction1", name);
	g_test_add (buf, SchedulerFixture, NULL, setup, destruction1, teardown);
}
gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	add_tests_with_fixture (iris_scheduler_fixture_setup,
	                        iris_scheduler_fixture_teardown,
	                        "default");
	add_tests_with_fixture (iris_gmainscheduler_fixture_setup,
	                        iris_gmainscheduler_fixture_teardown,
	                        "gmainscheduler");
	add_tests_with_fixture (iris_lfscheduler_fixture_setup,
	                        iris_lfscheduler_fixture_teardown,
	                        "lockfree-scheduler");
	add_tests_with_fixture (iris_wsscheduler_fixture_setup,
	                        iris_wsscheduler_fixture_teardown,
	                        "workstealing-scheduler");

	return g_test_run ();
}
