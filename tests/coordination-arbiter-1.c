#include <iris/iris.h>
#include <iris/iris-arbiter-private.h>
#include <iris/iris-coordination-arbiter-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-port-private.h>

G_LOCK_DEFINE (exclusive);
G_LOCK_DEFINE (concurrent);
G_LOCK_DEFINE (teardown);

static GMutex *sync = NULL;
static GCond  *cond = NULL;

static void
test1 (void)
{
	IrisPort     *port     = iris_port_new ();
	IrisReceiver *receiver = iris_arbiter_receive (NULL, port, NULL, NULL);
	IrisArbiter  *arbiter  = iris_arbiter_coordinate (receiver, NULL, NULL);
	g_assert (port);
	g_assert (receiver);
	g_assert (arbiter);
}

static void
test2_e (IrisMessage *message,
         gpointer     user_data)
{
	gboolean *exc_b = user_data;

	G_LOCK (exclusive);
	g_mutex_lock (sync);
	*exc_b = !(*exc_b);
	g_cond_signal (cond);
	g_mutex_unlock (sync);
	G_UNLOCK (exclusive);
}

static void
test2_c (IrisMessage *message,
         gpointer     user_data)
{
	gboolean *cnc_b = user_data;

	G_LOCK (concurrent);
	g_mutex_lock (sync);
	*cnc_b = !(*cnc_b);
	g_cond_signal (cond);
	g_mutex_unlock (sync);
	G_UNLOCK (concurrent);
}

static void
test2_t (IrisMessage *message,
         gpointer     user_data)
{
	gboolean *tdn_b= user_data;

	G_LOCK (teardown);
	*tdn_b = TRUE;
	G_UNLOCK (teardown);
}

static void
test2 (void)
{
	gboolean      exc_b   = FALSE,
	              cnc_b   = FALSE,
	              tdn_b   = FALSE;
	IrisPort     *exc     = iris_port_new (),
	             *cnc     = iris_port_new (),
	             *tdn     = iris_port_new ();
	IrisReceiver *exc_r   = iris_arbiter_receive (NULL, exc, test2_e, &exc_b),
	             *cnc_r   = iris_arbiter_receive (NULL, cnc, test2_c, &cnc_b),
	             *tdn_r   = iris_arbiter_receive (NULL, tdn, test2_t, &tdn_b);
	IrisArbiter  *arbiter = iris_arbiter_coordinate (exc_r, cnc_r, tdn_r);

	sync = g_mutex_new ();
	cond = g_cond_new ();

	/******************************************************************/
	/*          BEGIN TEST PART ONE, EXCLUSIVE MESSAGE SEND           */
	/******************************************************************/

	/* make sure exclusive will block when called */
	g_mutex_lock (sync);

	/* push a message to exclusive, the handling thread will block,
	 * keeping us with an active count of 1. Make sure that the item
	 * is delivered properly. */
	iris_port_post (exc, iris_message_new (1));
	g_assert (exc->priv->current == NULL);
	g_assert (exc->priv->queue == NULL);
	g_assert (exc_r->priv->message == NULL);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);

	/* Send another message for exclusive, this should NOT get executed
	 * right away since the other exclusive is active */
	iris_port_post (exc, iris_message_new (1));
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
	g_assert (exc_r->priv->message != NULL);
	g_assert (iris_port_is_paused (exc));

	/* Make sure the other thread is blocked, and holds the active, we will
	 * hold the arbiter lock so we know it cannot progress until we do our
	 * active check. */
	g_mutex_lock (IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex);
	g_cond_wait (cond, sync);
	g_assert (exc_b == TRUE);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
	g_assert (exc_r->priv->message != NULL);
	g_assert (iris_port_is_paused (exc));
	g_mutex_unlock (IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex);

	/*****************************************************************/
	/*  BEGIN TEST PART TWO, CONCURRENT SEND WHILE EXCLUSIVE ACTIVE  */
	/*****************************************************************/

	g_usleep (G_USEC_PER_SEC / 100); // Wait while switching

	/* We still have an exclusive blocked at this point while we hold
	 * onto our sync mutex.  We need to wait on cond for the concurrent
	 * to move forward (would get activated on the completion of exc) */

	iris_port_post (cnc, iris_message_new (1));
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_NEEDS_ANY),==,IRIS_COORD_NEEDS_CONCURRENT);
	g_assert (cnc_r->priv->message != NULL);
	g_assert (iris_port_is_paused (cnc));

	/* Signal the exclusive thread, allowing it to complete and then
	 * switch to the concurrent mode. */
	g_mutex_lock (IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex);
	g_cond_wait (cond, sync);
	g_assert (exc_b == FALSE);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
	g_assert (exc->priv->current == NULL);
	g_assert (exc->priv->queue == NULL);
	g_assert (cnc_r->priv->message != NULL);
	g_assert (iris_port_is_paused (cnc));
	g_mutex_unlock (IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex);

	/* The arbiter should now be switching to the concurrent mode */
	g_usleep (G_USEC_PER_SEC / 100);

	/* Make sure that we have switched to concurrent, and that there
	 * is one item in it (our current blocked on sync). */
	g_assert (cnc->priv->current == NULL);
	g_assert (cnc->priv->queue == NULL);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);

	/* send 5 more messages to make our total count up to 6 */
	iris_port_post (cnc, iris_message_new (1));
	iris_port_post (cnc, iris_message_new (1));
	iris_port_post (cnc, iris_message_new (1));
	iris_port_post (cnc, iris_message_new (1));
	iris_port_post (cnc, iris_message_new (1));

	/* now all 6 are blocked on our mutex until we wait for them */
	g_assert (cnc->priv->current == NULL);
	g_assert (cnc->priv->queue == NULL);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,6);

	/* let 2 of them complete */
	g_cond_wait (cond, sync);
	g_cond_wait (cond, sync);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,4);

	/* and 2 more */
	g_cond_wait (cond, sync);
	g_cond_wait (cond, sync);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,2);

	g_cond_wait (cond, sync);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
}

gint
main (int   argc,
      char *argv[])
{
	g_type_init ();
	g_test_init (&argc, &argv, NULL);
	g_thread_init (NULL);

	g_test_add_func ("/coordination-arbiter/coordinate1", test1);
	g_test_add_func ("/coordination-arbiter/can_receive1", test2);

	return g_test_run ();
}
