#include <iris.h>
#include <iris/iris-coordination-arbiter.h>
#include <iris/iris-coordination-arbiter-private.h>
#include <iris/iris-receiver-private.h>
#include <iris/iris-port-private.h>

G_LOCK_DEFINE (exclusive);
G_LOCK_DEFINE (concurrent);
G_LOCK_DEFINE (teardown);

static GMutex *mutex[10] = { NULL, };
static GCond  *cond[10]  = { NULL, };

static void
test1 (void)
{
	IrisPort     *port     = iris_port_new ();
	IrisReceiver *receiver = iris_arbiter_receive (NULL, port, NULL, NULL, NULL);
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

	g_mutex_lock (mutex [message->what]);
	*exc_b = !(*exc_b);
	g_cond_signal (cond [message->what]);
	g_mutex_unlock (mutex [message->what]);
}

static void
test2_c (IrisMessage *message,
         gpointer     user_data)
{
	gboolean *cnc_b = user_data;

	g_mutex_lock (mutex [message->what]);
	*cnc_b = !(*cnc_b);
	g_cond_signal (cond [message->what]);
	g_mutex_unlock (mutex [message->what]);
}

static void
test2_t (IrisMessage *message,
         gpointer     user_data)
{
	gboolean *tdn_b= user_data;
	*tdn_b = TRUE;
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
	IrisReceiver *exc_r   = iris_arbiter_receive (NULL, exc, test2_e, &exc_b, NULL),
	             *cnc_r   = iris_arbiter_receive (NULL, cnc, test2_c, &cnc_b, NULL),
	             *tdn_r   = iris_arbiter_receive (NULL, tdn, test2_t, &tdn_b, NULL);
	IrisArbiter  *arbiter = iris_arbiter_coordinate (exc_r, cnc_r, tdn_r);
	gint          i;

	for (i = 1; i < 10; i++) {
		mutex [i] = g_mutex_new ();
		cond [i] = g_cond_new ();
		g_mutex_lock (mutex [i]);
	}

	/******************************************************************/
	/*          BEGIN TEST PART ONE, EXCLUSIVE MESSAGE SEND           */
	/******************************************************************/

	/* push a message to exclusive, the handling thread will block,
	 * keeping us with an active count of 1. Make sure that the item
	 * is delivered properly. */
	iris_port_post (exc, iris_message_new (1));
	g_assert (exc->priv->current == NULL);
	g_assert (exc->priv->queue == NULL);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);

	/* Send another message for exclusive, this should NOT get executed
	 * right away since the other exclusive is active */
	iris_port_post (exc, iris_message_new (2));
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
	g_assert (iris_port_is_paused (exc));

	/* Make sure the other thread is blocked, and holds the active, we will
	 * hold the arbiter lock so we know it cannot progress until we do our
	 * active check. */
	g_static_rec_mutex_lock (&IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex);
	g_cond_wait (cond [1], mutex [1]);
	g_assert (exc_b == TRUE);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
	g_assert (iris_port_is_paused (exc));

	/*****************************************************************/
	/*  BEGIN TEST PART TWO, CONCURRENT SEND WHILE EXCLUSIVE ACTIVE  */
	/*****************************************************************/

	/* We still have an exclusive blocked at this point while we hold
	 * onto our sync mutex.  We need to wait on cond for the concurrent
	 * to move forward (would get activated on the completion of exc) */

	iris_port_post (cnc, iris_message_new (3));
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,1);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_NEEDS_ANY),==,IRIS_COORD_NEEDS_CONCURRENT | IRIS_COORD_NEEDS_EXCLUSIVE);
	g_static_rec_mutex_unlock (&IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex); // allow first exclusive to finish
	g_assert (iris_port_is_paused (cnc));

	/* Signal the second exclusive thread, allowing it to complete and then
	 * switch to the concurrent mode. */
	g_cond_wait (cond [2], mutex [2]);
	g_assert (exc_b == FALSE);

	/* make sure exclusive really is done */
	g_assert (exc->priv->current == NULL);
	g_assert (exc->priv->queue == NULL);

	/* The arbiter should now be switching to the concurrent mode.
	 * we can wait on the first concurrent cond so we know we are in
	 * concurrent mode.
	 */
	g_cond_wait (cond [3], mutex [3]);

	/* kinda racey */
	g_static_rec_mutex_lock (&IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex);

	/* Make sure that we have switched to concurrent, and that there
	 * is one item in it (our current blocked on sync). */
	g_assert (cnc->priv->current == NULL);
	g_assert (cnc->priv->queue == NULL);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);

	/* These should not be blocked from starting, but wont be able to finish */
	/* send 5 more messages to make our total count up to 5 (or 6 if we raced previously) */
	iris_port_post (cnc, iris_message_new (4));
	iris_port_post (cnc, iris_message_new (5));
	iris_port_post (cnc, iris_message_new (6));
	iris_port_post (cnc, iris_message_new (7));
	iris_port_post (cnc, iris_message_new (8));

	/* now all 6 are blocked on our mutex until we wait for them */
	g_assert (cnc->priv->current == NULL);
	g_assert (cnc->priv->queue == NULL);
	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,>=,5);

	/* let them finish */
	g_static_rec_mutex_unlock (&IRIS_COORDINATION_ARBITER (arbiter)->priv->mutex);

	/* let 4,5,6 finish */
	g_cond_wait (cond [4], mutex [4]);
	g_cond_wait (cond [5], mutex [5]);
	g_cond_wait (cond [6], mutex [6]);

	/* again, racey */
	g_usleep (G_USEC_PER_SEC / 50);

	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,2);

	/* let the rest finish */
	g_cond_wait (cond [7], mutex [7]);
	g_cond_wait (cond [8], mutex [8]);

	g_usleep (G_USEC_PER_SEC / 50);

	g_assert_cmpint ((IRIS_COORDINATION_ARBITER (arbiter)->priv->flags & IRIS_COORD_ANY),==,IRIS_COORD_CONCURRENT);
	g_assert_cmpint (IRIS_COORDINATION_ARBITER (arbiter)->priv->active,==,0);
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
