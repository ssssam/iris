#include <iris/iris.h>
#include <iris/iris-coordination-arbiter-private.h>
#include <mocks/mock-scheduler.h>

static void
test1 (void)
{
	IrisPort     *port     = iris_port_new ();
	IrisReceiver *receiver = iris_arbiter_receive (NULL, port, NULL, NULL);
	IrisArbiter  *arbiter  = iris_arbiter_coordinate (receiver, NULL, NULL);
	g_assert (port);
	g_assert (receiver);
	g_assert (arbiter);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/coordination-arbiter/coordinate", test1);

	return g_test_run ();
}
