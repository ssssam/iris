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
	iris_stack_free (stack);
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
	g_test_add_func ("/stack/free", test4);

	return g_test_run ();
}
