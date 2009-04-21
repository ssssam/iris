#include <iris/iris.h>

static GMutex *mutex;
static GCond  *cond;

void
worker (IrisTask *task,
        gpointer  user_data)
{
	GList       *list = NULL;
	GDir        *dir;
	const gchar *name;

	dir = g_dir_open ((gchar*)user_data, 0, NULL);

	while ((name = g_dir_read_name (dir)) != NULL) {
		list = g_list_prepend (list, g_strdup (name));
	}

	IRIS_TASK_RETURN_VALUE (task, G_TYPE_POINTER, list);
}

void
callback (IrisTask *task,
          gpointer  user_data)
{
	GList *result, *iter;

	result = g_value_get_pointer (iris_task_get_result (task));

	for (iter = result; iter; iter = iter->next) {
		g_print ("%s\n", (gchar*)iter->data);
		g_free (iter->data);
	}

	g_mutex_lock (mutex);
	g_cond_signal (cond);
	g_mutex_unlock (mutex);
}

gint
main (gint   argc,
      gchar *argv[])
{
	IrisTask *task = NULL;
	gchar    *dir  = g_get_current_dir ();

	if (argc > 1)
		dir = g_strdup (argv [1]);

	iris_init ();

	mutex = g_mutex_new ();
	cond = g_cond_new ();

	g_mutex_lock (mutex);
	task = iris_task_new (worker, dir, NULL);
	iris_task_add_callback (task, callback, NULL, NULL);
	iris_task_run (task);

	g_cond_wait (cond, mutex);
	g_mutex_unlock (mutex);

	g_free (dir);

	return 0;
}
