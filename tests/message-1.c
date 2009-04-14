#include <iris/iris.h>
#include <string.h>

static void
ref_count1 (void)
{
	IrisMessage *msg;
	
	msg = iris_message_new (1);
	g_assert (msg != NULL);
	g_assert_cmpint (msg->ref_count, ==, 1);

	iris_message_unref (msg);
	g_assert_cmpint (msg->ref_count, ==, 0);
}

static void
ref_count2 (void)
{
	IrisMessage *msg;
	
	msg = iris_message_new (1);
	g_assert (msg != NULL);
	g_assert_cmpint (msg->ref_count, ==, 1);

	iris_message_ref (msg);
	g_assert_cmpint (msg->ref_count, ==, 2);

	iris_message_unref (msg);
	g_assert_cmpint (msg->ref_count, ==, 1);

	iris_message_unref (msg);
	g_assert_cmpint (msg->ref_count, ==, 0);
}

static void
set_string1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new (1);
	g_assert (msg != NULL);

	iris_message_set_string (msg, "id", "1234567890");
	g_assert_cmpstr (iris_message_get_string (msg, "id"), ==, "1234567890");
}

static void
set_int1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new (1);
	g_assert (msg != NULL);

	iris_message_set_int (msg, "id", 1234567890);
	g_assert_cmpint (iris_message_get_int (msg, "id"), ==, 1234567890);
}

static void
copy1 (void)
{
	IrisMessage *msg;
	IrisMessage *msg2;

	msg = iris_message_new (1);
	g_assert (msg != NULL);

	iris_message_set_int (msg, "id", 1234567890);
	g_assert_cmpint (iris_message_get_int (msg, "id"), ==, 1234567890);

	msg2 = iris_message_copy (msg);
	g_assert (msg2 != NULL);
	g_assert_cmpint (iris_message_get_int (msg2, "id"), ==, 1234567890);
}

static void
count_names1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new (1);
	g_assert (msg != NULL);
	g_assert_cmpint (iris_message_count_names (msg), ==, 0);

	iris_message_set_int (msg, "id", 1234567890);
	g_assert_cmpint (iris_message_count_names (msg), ==, 1);
}

static void
get_type1 (void)
{
	g_assert_cmpint (IRIS_TYPE_MESSAGE, !=, G_TYPE_INVALID);
}

static void
is_empty1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new (1);
	g_assert (msg != NULL);
	g_assert (iris_message_is_empty (msg));

	iris_message_set_int (msg, "id", 1234567890);
	g_assert (!iris_message_is_empty (msg));
}

static void
new_full1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1,
	                             "id", G_TYPE_INT, 1234567890,
	                             "name", G_TYPE_STRING, "Christian",
	                             NULL);
	g_assert (msg != NULL);

	g_assert_cmpint (iris_message_get_int (msg, "id"), ==, 1234567890);
	g_assert_cmpstr (iris_message_get_string (msg, "name"), ==, "Christian");
}

static void
flattened_size1 (void)
{
	IrisMessage *msg;
	gsize length;

	msg = iris_message_new_full (1,
	                             "id", G_TYPE_INT, 1234567890,
	                             "name", G_TYPE_STRING, "Christian",
	                             NULL);
	g_assert (msg != NULL);

	length = iris_message_flattened_size (msg);
	g_assert_cmpint (length, ==, 4         /* Message Type */
	                           + 2 + 4     /* Id Value Type and Size */
	                           + 2 + 4 + 9 /* Name Value Type and Size and Content */
	                           + 4 + strlen ("id")
	                           + 4 + strlen ("name"));
}

static void
contains1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_INT, 0, NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_contains (msg, "id"));
	g_assert (!iris_message_contains (msg, "name"));
}

static void
set_int641 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_INT64, G_MAXINT64, NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_int64 (msg, "id") == G_MAXINT64);
}

static void
set_float1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_FLOAT, 1.2345f, NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_float (msg, "id") == 1.2345f);
}

static void
set_double1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_DOUBLE, 21.123456, NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_double (msg, "id") == 21.123456);
}

static void
set_long1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_LONG, 123123l, NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_long (msg, "id") == 123123l);
}

static void
set_ulong1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_ULONG, 123123ul, NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_ulong (msg, "id") == 123123ul);
}

static void
set_char1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_CHAR, 'A', NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_char (msg, "id") == 'A');
}

static void
set_uchar1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_UCHAR, 'A', NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_uchar (msg, "id") == 'A');
}

static void
set_boolean1 (void)
{
	IrisMessage *msg;

	msg = iris_message_new_full (1, "id", G_TYPE_BOOLEAN, TRUE, NULL);
	g_assert (msg != NULL);
	g_assert (iris_message_get_boolean (msg, "id") == TRUE);
}

static void
million_create (void)
{
	IrisMessage *msg;
	int i;

	GTimer *timer = g_timer_new ();

	g_timer_start (timer);

	for (i = 0; i < 1000000; i++) {
		//msg = iris_message_new_full (1, "id", G_TYPE_INT, 1, NULL);
		msg = iris_message_new (1);
		//iris_message_unref (msg);
	}

	g_timer_stop (timer);

	//g_debug ("Ellapsed %lf", g_timer_elapsed (timer, NULL));
}

static void
set_data1 (void)
{
	GValue value = {0,};
	IrisMessage *msg = iris_message_new (1);
	const gchar *str;

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "This is my string");
	iris_message_set_data (msg, &value);

	str = g_value_get_string (iris_message_get_data (msg));
	g_assert_cmpstr (str, ==, "This is my string");
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/message/get_type1", get_type1);
	g_test_add_func ("/message/ref_count1", ref_count1);
	g_test_add_func ("/message/ref_count2", ref_count2);
	g_test_add_func ("/message/set_string1", set_string1);
	g_test_add_func ("/message/set_int1", set_int1);
	g_test_add_func ("/message/copy1", copy1);
	g_test_add_func ("/message/count_names1", count_names1);
	g_test_add_func ("/message/is_empty1", count_names1);
	g_test_add_func ("/message/new_full1", count_names1);
	g_test_add_func ("/message/flattened_size1", flattened_size1);
	g_test_add_func ("/message/contains1", flattened_size1);
	g_test_add_func ("/message/set_int641", set_int641);
	g_test_add_func ("/message/set_float1", set_float1);
	g_test_add_func ("/message/set_double1", set_double1);
	g_test_add_func ("/message/set_long1", set_long1);
	g_test_add_func ("/message/set_ulong1", set_ulong1);
	g_test_add_func ("/message/set_char1", set_char1);
	g_test_add_func ("/message/set_uchar1", set_uchar1);
	g_test_add_func ("/message/set_boolean1", set_boolean1);
	g_test_add_func ("/message/million_create", million_create);
	g_test_add_func ("/message/set_data1", set_data1);

	return g_test_run ();
}
