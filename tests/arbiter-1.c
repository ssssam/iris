#include <iris.h>
#include <iris/iris-coordination-arbiter.h>
#include <iris/iris-coordination-arbiter-private.h>
#include <mocks/mock-scheduler.h>

static IrisScheduler *default_scheduler = NULL;

#define SETUP()                                                 \
	G_STMT_START {                                          \
		default_scheduler = mock_scheduler_new();       \
		iris_scheduler_set_default(default_scheduler);  \
	} G_STMT_END
#define COORD_FLAG_ON(a,f) ((IRIS_COORDINATION_ARBITER (a)->priv->flags & f) != 0)
#define COORD_FLAG_SET(a,f) ((IRIS_COORDINATION_ARBITER (a)->priv->flags = f) != 0)

static void
test1 (void)
{
	g_assert (iris_arbiter_receive (NULL, iris_port_new (), NULL, NULL, NULL) != NULL);
}

static void
test2_cb (IrisMessage *msg,
          gpointer     data)
{
	gboolean *b = data;
	*b = TRUE;
}

static void
test2 (void)
{
	SETUP ();
	gboolean success = FALSE;
	IrisPort *port = iris_port_new ();
	iris_arbiter_receive (NULL, port, test2_cb, &success, NULL);
	iris_port_post (port, iris_message_new (1));
	g_assert (success == TRUE);
}

static void
test3 (void)
{
	SETUP ();

	IrisArbiter *arbiter;

	arbiter = iris_arbiter_coordinate (
			iris_arbiter_receive (
				NULL,
				iris_port_new (),
				NULL,
				NULL,
				NULL),
			NULL,
			NULL);

	g_assert (arbiter != NULL);
}

static void
test4_cb (IrisMessage *msg,
            gpointer     data)
{
	gint *i = data;
	g_atomic_int_inc (i);
}

static void
test4 (void)
{
	SETUP ();

	IrisCoordinationArbiter *coord;
	IrisArbiter  *arbiter;
	IrisPort     *e_port, *c_port;
	IrisReceiver *e_recv, *c_recv;
	gint          e = 0, c = 0;

	e_port = iris_port_new ();
	c_port = iris_port_new ();

	g_assert (e_port);
	g_assert (c_port);

	e_recv = iris_arbiter_receive (mock_scheduler_new (), e_port, test4_cb, &e, NULL);
	c_recv = iris_arbiter_receive (mock_scheduler_new (), c_port, test4_cb, &c, NULL);

	g_assert (e_recv);
	g_assert (c_recv);

	arbiter = iris_arbiter_coordinate (e_recv, c_recv, NULL);
	coord = IRIS_COORDINATION_ARBITER (arbiter);
	
	g_assert (arbiter);

	iris_port_post (e_port, iris_message_new (1));
	g_assert_cmpint (coord->priv->flags & IRIS_COORD_ANY, ==, IRIS_COORD_EXCLUSIVE);
	g_assert_cmpint (e, ==, 1);

	iris_port_post (c_port, iris_message_new (1));
	g_assert (COORD_FLAG_ON (arbiter, IRIS_COORD_CONCURRENT));
	iris_port_post (c_port, iris_message_new (1));
	g_assert (COORD_FLAG_ON (arbiter, IRIS_COORD_CONCURRENT));
	iris_port_post (c_port, iris_message_new (1));
	g_assert (COORD_FLAG_ON (arbiter, IRIS_COORD_CONCURRENT));
	g_assert_cmpint (c, ==, 3);

	iris_port_post (e_port, iris_message_new (1));
	g_assert (COORD_FLAG_ON (arbiter, IRIS_COORD_EXCLUSIVE));
	g_assert_cmpint (e, ==, 2);

	iris_port_post (c_port, iris_message_new (1));
	g_assert (COORD_FLAG_ON (arbiter, IRIS_COORD_CONCURRENT));
	g_assert_cmpint (c, ==, 4);

	/* this is a sort of dead state, as its waiting for the exclusive
	 * to unpause and flush out items into the arbiter. the message
	 * wont be delivered now. */
	COORD_FLAG_SET (arbiter, IRIS_COORD_CONCURRENT | IRIS_COORD_NEEDS_EXCLUSIVE);
	iris_port_post (c_port, iris_message_new (1));
	g_assert_cmpint (c, ==, 4);

	/* By passing another exclusive message in, we can finish off
	 * our hypothetical wait for NEEDS_EXCLUSIVE and then causing the
	 * concurrent to also process. Ultimately increasing both. */
	iris_port_post (e_port, iris_message_new (1));
	g_assert_cmpint (e, ==, 3);
	g_assert_cmpint (c, ==, 5);
}

gint
main (int   argc,
      char *argv[])
{
	iris_init ();
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/arbiter/receive1", test1);
	g_test_add_func ("/arbiter/receive2", test2);
	g_test_add_func ("/arbiter/coordinate1", test3);
	g_test_add_func ("/arbiter/coordinate2", test4);

	return g_test_run ();
}
