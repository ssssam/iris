#include <iris/iris.h>

#define ITER_MAX      1000
#define EXCLUSIVE_MOD 100

static GMutex *mutex = NULL;
static GCond  *cond  = NULL;

static void
exclusive_handler (IrisMessage *message,
                   gpointer     user_data)
{
}

static void
concurrent_handler (IrisMessage *message,
                    gpointer     user_data)
{
}

static void
teardown_handler (IrisMessage *message,
                  gpointer     user_data)
{
	g_mutex_lock (mutex);
	g_cond_signal (cond);
	g_mutex_unlock (mutex);
}

static void
coordinator (void)
{
	IrisPort     *exclusive    = iris_port_new (),
	             *concurrent   = iris_port_new (),
	             *teardown     = iris_port_new ();
	IrisReceiver *exclusive_r  = iris_arbiter_receive (NULL, exclusive,  exclusive_handler, NULL),
	             *concurrent_r = iris_arbiter_receive (NULL, concurrent, concurrent_handler, NULL),
	             *teardown_r   = iris_arbiter_receive (NULL, concurrent, teardown_handler, NULL);
	IrisArbiter  *arbiter      = iris_arbiter_coordinate (exclusive_r, concurrent_r, teardown_r);
	IrisMessage  *message;
	gint          i;

	mutex = g_mutex_new ();
	g_mutex_lock (mutex);
	cond = g_cond_new ();

	for (i = 0; i < ITER_MAX; i++) {
		message = iris_message_new (1);
		iris_port_post ((i % EXCLUSIVE_MOD == 0) ? exclusive : concurrent, message);
		iris_message_unref (message);
	}

	iris_port_post (teardown, iris_message_new (1));
	g_cond_wait (cond, mutex);
	g_mutex_unlock (mutex);
}

int
main (int   argc,
      char *argv[])
{
	iris_init ();
	coordinator ();
	return 0;
}
