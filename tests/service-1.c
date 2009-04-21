#include <iris/iris.h>
#include "mocks/mock-service.h"

static void
test1 (void)
{
	g_assert (mock_service_new () != NULL);
}

static void
test2 (void)
{
	IrisService *service;
	service = mock_service_new ();
	iris_service_start (service);
	g_assert (iris_service_is_started (service));
}

static void
test3 (void)
{
	IrisService *service;
	service = mock_service_new ();
	iris_service_start (service);
	g_assert (iris_service_is_started (service));
	iris_service_stop (service);
	g_assert (!iris_service_is_started (service));
}

gint
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	iris_init ();

	g_test_add_func ("/service/new", test1);
	g_test_add_func ("/service/start", test2);
	g_test_add_func ("/service/stop", test3);

	return g_test_run ();
}
