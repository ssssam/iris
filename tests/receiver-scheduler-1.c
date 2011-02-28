#include <iris.h>
#include <string.h>

#include <iris/iris-arbiter-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-scheduler-private.h>

#include "g-repeated-test.h"

/* FIXME: maybe rename this test suite to messages-live-1 or something */

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
	fixture->scheduler = iris_scheduler_new_full (1, 2);
}

static void
iris_scheduler_fixture_teardown (SchedulerFixture *fixture,
                                 gconstpointer    user_data)
{
	/* Make sure receivers etc. aren't holding extra references */
	g_assert_cmpint (G_OBJECT(fixture->scheduler)->ref_count, ==, 1);
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
yield (SchedulerFixture *fixture, int count)
{
	gint j;

	if (fixture->main_loop != NULL) 
		for (j = 0; j < count; j++)
			g_main_context_iteration (NULL, FALSE);
		else
			g_usleep (count * 100);
}

static void
message_handler (IrisMessage *message,
                 gpointer     user_data)
{
	gint      message_number = message->what;
	gint     *p_counter = iris_message_get_pointer (message, "counter");
	gboolean  check_order = iris_message_get_boolean (message, "check-order");

	if (check_order)
		g_assert_cmpint (g_atomic_int_get (p_counter), ==, message_number);

	g_atomic_int_inc (p_counter);
}

static void
post_test_message (IrisPort *port,
                   gint     *p_counter,
                   gint      message_number,
                   gboolean  check_order)
{
	IrisMessage *message = iris_message_new (message_number);
	iris_message_set_pointer (message, "counter", p_counter);
	iris_message_set_boolean (message, "check-order", check_order);

	iris_port_post (port, message);
}


/* integrity: general check for races etc. by sending lots of messages really fast.
 * 
 * Called twice, the second time using an arbiter to catch problems with
 * queueing etc.
 */
static void
test_integrity (SchedulerFixture *fixture,
                gconstpointer     user_data)
{
	IrisPort      *port;
	IrisReceiver  *receiver;
	gint           message_counter,
	               j;
	const gboolean use_arbiter = GPOINTER_TO_INT (user_data);

	message_counter = 0;

	port = iris_port_new ();
	receiver = iris_arbiter_receive (fixture->scheduler,
	                                 port,
	                                 &message_handler, NULL, NULL);

	if (use_arbiter)
		iris_arbiter_coordinate (receiver, NULL, NULL);

	for (j=0; j<50; j++)
		post_test_message (port, &message_counter, j, FALSE);

	g_usleep (5000);
	post_test_message (port, &message_counter, 50, FALSE);

	while (g_atomic_int_get (&message_counter) < 51)
		yield (fixture, 50);

	iris_receiver_destroy (receiver, FALSE, NULL, IRIS_IS_GMAINSCHEDULER (fixture->scheduler));
	g_object_unref (port);
}

/* order: Ensure exclusive receivers work properly. */
static void
test_order (SchedulerFixture *fixture,
            gconstpointer     user_data)
{
	IrisPort      *port;
	IrisReceiver  *receiver;
	gint           message_counter,
	               j;

	message_counter = 0;

	port = iris_port_new ();
	receiver = iris_arbiter_receive (fixture->scheduler,
	                                 port,
	                                 &message_handler, NULL, NULL);
	iris_arbiter_coordinate (receiver, NULL, NULL);

	for (j=0; j<50; j++)
		post_test_message (port, &message_counter, j, TRUE);

	g_usleep (5000);
	post_test_message (port, &message_counter, 50, TRUE);

	while (g_atomic_int_get (&message_counter) < 51)
		yield (fixture, 50);

	g_object_unref (port);
	iris_receiver_destroy (receiver, FALSE, NULL, IRIS_IS_GMAINSCHEDULER (fixture->scheduler));
}

static void
test_destruction (SchedulerFixture *fixture,
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

	iris_receiver_destroy (r, FALSE, NULL, IRIS_IS_GMAINSCHEDULER (fixture->scheduler));

	yield (fixture, 50);

	g_object_unref (p);
}


static void
add_tests_with_fixture (void (*setup) (SchedulerFixture *, gconstpointer),
                        void (*teardown) (SchedulerFixture *, gconstpointer),
                        const char *name)
{
	char buf[256];

	g_snprintf (buf, 255, "/receiver-scheduler/%s/integrity 1", name);
	g_test_add (buf, SchedulerFixture, GINT_TO_POINTER (FALSE), setup,
	            test_integrity, teardown);

	g_snprintf (buf, 255, "/receiver-scheduler/%s/integrity 2", name);
	g_test_add_repeated (buf, 250, SchedulerFixture, GINT_TO_POINTER (TRUE), setup,
	                     test_integrity, teardown);

	g_snprintf (buf, 255, "/receiver-scheduler/%s/order", name);
	g_test_add_repeated (buf, 250, SchedulerFixture, NULL, setup, test_order,
	                     teardown);

	g_snprintf (buf, 255, "/receiver-scheduler/%s/destruction", name);
	g_test_add (buf, SchedulerFixture, NULL, setup, test_destruction, teardown);
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


	if (0) {
	add_tests_with_fixture (iris_lfscheduler_fixture_setup,
	                        iris_lfscheduler_fixture_teardown,
	                        "lockfree-scheduler");
	add_tests_with_fixture (iris_wsscheduler_fixture_setup,
	                        iris_wsscheduler_fixture_teardown,
	                        "workstealing-scheduler");
	}

	return g_test_run ();
}
