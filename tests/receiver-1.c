#include <iris/iris.h>
#include <string.h>

#include <iris/iris-arbiter-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-scheduler-private.h>

static void
get_type1 (void)
{
	g_assert (IRIS_TYPE_RECEIVER != G_TYPE_INVALID);
}

static void
new_full1 (void)
{
	IrisReceiver  *receiver;
	IrisScheduler *scheduler;
	IrisArbiter   *arbiter;

	scheduler = iris_scheduler_new ();
	arbiter = iris_arbiter_new ();
	receiver = iris_receiver_new_full (scheduler, arbiter);

	g_assert (IRIS_IS_SCHEDULER (scheduler));
	g_assert (IRIS_IS_ARBITER (arbiter));
	g_assert (IRIS_IS_RECEIVER (receiver));

	g_assert (iris_receiver_has_arbiter (receiver));
	g_assert (iris_receiver_has_scheduler (receiver));
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/port/get_type1", get_type1);
	g_test_add_func ("/port/new_full1", new_full1);

	return g_test_run ();
}
