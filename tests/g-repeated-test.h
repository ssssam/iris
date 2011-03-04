/* Utility to run a test x times.
 *
 * FIXME: would this be useful merged into glib? 
 */
typedef struct {
	gint          times;
	gboolean      has_data;
	gint          fixture_size;
	gconstpointer tdata;
	void (*fsetup)        (gpointer, gconstpointer);
	void (*ftest_fixture) (gpointer, gconstpointer);
	void (*fteardown)     (gpointer, gconstpointer);
	void (*ftest)         ();
	void (*ftest_data)    (gconstpointer);
} RepeatedTestCase;

static void repeated_test_func (gconstpointer data);

static void repeated_test_fixture (gconstpointer data)
{
	RepeatedTestCase *test_case = (gpointer)data;
	gpointer fixture;
	int i;

	/* FIXME: fix gcc with -Wall -Werror (fuck you -Werror) */
	if (0) repeated_test_func (NULL);

	fixture = g_slice_alloc (test_case->fixture_size);

	for (i=0; i<test_case->times; i++) {
		test_case->fsetup (fixture, test_case->tdata);
		test_case->ftest_fixture (fixture, test_case->tdata);
		test_case->fteardown (fixture, test_case->tdata);
	}

	g_slice_free1 (test_case->fixture_size, fixture);
	g_slice_free (RepeatedTestCase, test_case);
}

static void repeated_test_func (gconstpointer data)
{
	RepeatedTestCase *test_case = (gpointer)data;
	int i;

	/* FIXME: fix gcc with -Wall -Werror (fuck you -Werror) */
	if (0) repeated_test_fixture (NULL);

	for (i=0; i<test_case->times; i++) {
		if (test_case->has_data)
			test_case->ftest_data (test_case->tdata);
		else
			test_case->ftest ();
	}

	g_slice_free (RepeatedTestCase, test_case);
}

#define g_test_add_repeated(testpath, _times, Fixture, _tdata, _fsetup, _ftest, _fteardown) \
  G_STMT_START { \
      RepeatedTestCase *test_case = g_slice_new (RepeatedTestCase); \
      test_case->times = _times;    \
      test_case->fixture_size = sizeof(Fixture); \
      test_case->tdata = _tdata; \
      test_case->fsetup = (void (*)(gpointer, gconstpointer))_fsetup; \
      test_case->ftest_fixture = (void (*)(gpointer, gconstpointer))_ftest; \
      test_case->fteardown =(void (*)(gpointer, gconstpointer)) _fteardown; \
      g_test_add_data_func (testpath, test_case, repeated_test_fixture); \
  } G_STMT_END

#define g_test_add_func_repeated(testpath, _times, _ftest)            \
  G_STMT_START {                                                      \
      RepeatedTestCase *test_case = g_slice_new (RepeatedTestCase);   \
      test_case->has_data = FALSE;                                    \
      test_case->times = _times;                                      \
      test_case->ftest = _ftest;                                      \
      g_test_add_data_func (testpath, test_case, repeated_test_func); \
  } G_STMT_END

#define g_test_add_data_func_repeated(testpath, _times, _tdata, _ftest) \
  G_STMT_START {                                                        \
      RepeatedTestCase *test_case = g_slice_new (RepeatedTestCase);     \
      test_case->has_data = TRUE;                                       \
      test_case->times = _times;                                        \
      test_case->tdata = _tdata;                                        \
      test_case->ftest_data = _ftest;                                   \
      g_test_add_data_func (testpath, test_case, repeated_test_func);   \
  } G_STMT_END
