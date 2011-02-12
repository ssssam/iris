#include <iris.h>
#include <iris/iris-free-list.h>

static void
test1 (void)
{
	IrisFreeList *free_list = iris_free_list_new ();
	g_assert (free_list != NULL);
	g_assert (free_list->head != NULL);
	g_assert (free_list->head->next == NULL);
}

static void
test2 (void)
{
	IrisFreeList *free_list;

	free_list = iris_free_list_new ();
	g_assert (iris_free_list_get (free_list) != NULL);
}

static void
test3 (void)
{
	IrisFreeList *free_list;
	gint i;
	IrisLink *links[1000];
	
	free_list = iris_free_list_new ();
	for (i = 0; i < 1000; i++)
		links [i] = iris_free_list_get (free_list);
	for (i = 0; i < 1000; i++)
		iris_free_list_put (free_list, links [i]);
}

static void
test4 (void)
{
	IrisFreeList *free_list = iris_free_list_new ();
	g_assert (free_list != NULL);
	g_assert (free_list->head != NULL);
	g_assert (free_list->head->next == NULL);
	iris_free_list_free (free_list);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/freelist/new", test1);
	g_test_add_func ("/freelist/get", test2);
	g_test_add_func ("/freelist/put", test3);
	g_test_add_func ("/freelist/free", test4);

	return g_test_run ();
}
