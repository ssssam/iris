#include <iris/iris.h>

static void
test1 (void)
{
	IrisStack *stack = iris_stack_new ();
	g_assert (stack != NULL);
	g_assert (stack->head != NULL);
	g_assert (stack->head->next == NULL);
}

static void
test2 (void)
{
	IrisStack *stack = iris_stack_new ();
	g_assert (iris_stack_pop (stack) == NULL);
}

static void
test3 (void)
{
	gint i;

	IrisStack *stack = iris_stack_new ();
	g_assert (stack != NULL);
	iris_stack_push (stack, &i);
	g_assert (iris_stack_pop (stack) == &i);
}

static void
test4 (void)
{
	IrisStack *stack = iris_stack_new ();
	g_assert (stack != NULL);
	g_assert (stack->head != NULL);
	g_assert (stack->head->next == NULL);
	iris_stack_unref (stack);
}

static void
test5 (void)
{
	IrisStack *stack = iris_stack_new ();
	g_assert (stack != NULL);
	g_assert (stack->head != NULL);
	g_assert (stack->head->next == NULL);
	g_assert_cmpint (stack->ref_count, ==, 1);
	stack = iris_stack_ref (stack);
	g_assert_cmpint (stack->ref_count, ==, 2);
	iris_stack_unref (stack);
	g_assert_cmpint (stack->ref_count, ==, 1);
	iris_stack_unref (stack);
}

static void
test6 (void)
{
	g_assert_cmpint (IRIS_TYPE_STACK, !=, G_TYPE_INVALID);
}

int
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/stack/new", test1);
	g_test_add_func ("/stack/pop_empty", test2);
	g_test_add_func ("/stack/push_pop", test3);
	g_test_add_func ("/stack/unref", test4);
	g_test_add_func ("/stack/ref-unref", test5);
	g_test_add_func ("/stack/get_type", test6);

	return g_test_run ();
}
