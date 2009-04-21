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

static void
test4_cb (gpointer data)
{
	gboolean *success = data;
	*success = TRUE;
}

static void
test4 (void)
{
	IrisService *service;
	gboolean     success = FALSE;
	service = mock_service_new ();
	g_assert (service);
	iris_service_start (service);
	mock_service_send_exclusive (MOCK_SERVICE (service), G_CALLBACK (test4_cb), &success);
	g_assert (success == TRUE);
}

static void
test5_cb (gpointer data)
{
	gint *counter = data;
	g_atomic_int_inc (counter);
}

static void
test5 (void)
{
	IrisService *service;
	gint counter = 0;
	service = mock_service_new ();
	g_assert (service);
	iris_service_start (service);
	mock_service_send_concurrent (MOCK_SERVICE (service), G_CALLBACK (test5_cb), &counter);
	mock_service_send_concurrent (MOCK_SERVICE (service), G_CALLBACK (test5_cb), &counter);
	mock_service_send_concurrent (MOCK_SERVICE (service), G_CALLBACK (test5_cb), &counter);
	mock_service_send_concurrent (MOCK_SERVICE (service), G_CALLBACK (test5_cb), &counter);
	mock_service_send_concurrent (MOCK_SERVICE (service), G_CALLBACK (test5_cb), &counter);
	g_assert (counter == 5);
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
	g_test_add_func ("/service/exclusive", test4);
	g_test_add_func ("/service/concurrent", test5);

	return g_test_run ();
}
