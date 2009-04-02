/* iris-scheduler-manager.c
 *
 * Copyright (C) 2009 Christian Hergert <chris@dronelabs.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 
 * 02110-1301 USA
 */

#include "iris-scheduler-manager.h"
#include "iris-thread.h"

typedef struct
{
	GList *free_list;
	GList *all_list;
} IrisSchedulerManager;

/* Singleton instance of our scheduler manager struct */
static IrisSchedulerManager *singleton = NULL;

/* Lock for syncrhonizing intitialization. */
G_LOCK_DEFINE (singleton);

/**
 * iris_scheduler_manager_yield:
 *
 *
 * Checks and performs any work required to rebalance the scheduler managers
 * threads.  If the method returns %TRUE, then the calling thread should
 * exit its current scheduler and move on to other work.
 */
gboolean
iris_scheduler_manager_yield (void)
{
	/* return FALSE if the thread can continue handling the scheduler  */
	//return FALSE;

	return TRUE;
}

/**
 * get_or_create_thread:
 * @exclusive: if the thread should try to yield when done processing
 * @queue: a location to store the async queue
 *
 * Tries to first retreive a thread from the free thread list.  If that
 * fails, then a new thread is created.  If @try_yield is %TRUE then
 * the thread will try to yield itself back to the scheduler if it has
 * not received a work item within the timeout period.
 *
 * See iris_thread_manage()
 *
 * Return value: A re-purposed or new IrisThread
 */
static IrisThread*
get_or_create_thread (gboolean exclusive)
{
	IrisThread *thread;

	G_LOCK (singleton);

	if (singleton->free_list) {
		thread = singleton->free_list->data;
		singleton->free_list = g_list_remove (singleton->free_list,
		                                      singleton->free_list);
	}

	G_UNLOCK (singleton);

	if (!thread) {
		thread = iris_thread_new (exclusive);
		g_assert (thread != NULL); // FIXME: Add proper error handling
		singleton->all_list = g_list_prepend (singleton->all_list, thread);
	}

	return thread;
}

/**
 * iris_scheduler_manager_init:
 *
 * Initializes the internal state required for the scheduler manager.
 */
static void
iris_scheduler_manager_init (void)
{
	G_LOCK (singleton);
	if (G_LIKELY (!singleton))
		singleton = g_slice_new0 (IrisSchedulerManager);
	G_UNLOCK (singleton);
}

/**
 * iris_scheduler_manager_prepare:
 * @scheduler: An #IrisScheduler
 *
 * Prepares a scheduler for execution.  Any required threads for
 * processing are attached to the scheduler for future use.
 */
void
iris_scheduler_manager_prepare (IrisScheduler *scheduler)
{
	IrisThread *thread      = NULL;
	gint        max_threads = 0;
	gint        min_threads = 0;
	gint        i;

	if (G_UNLIKELY (!singleton))
		iris_scheduler_manager_init ();

	min_threads = iris_scheduler_get_min_threads (scheduler);
	g_assert (min_threads > 0);

	max_threads = iris_scheduler_get_max_threads (scheduler);
	g_assert ((max_threads >= min_threads) || max_threads == 0);

	for (i = 0; i < min_threads; i++) {
		thread = get_or_create_thread (FALSE);

		/* Add proper error handling */
		g_return_if_fail (thread != NULL);

		thread->scheduler = (gpointer)scheduler;
		iris_scheduler_add_thread (scheduler, thread);
	}
}

/**
 * iris_scheduler_manager_balance:
 *
 * Rebalances the threads available to provide more resources to
 * the schedulers that have not maxed out.  Schedulers that are
 * getting more backed up will be provided more threads if they
 * have not reached their thread limit.
 */
void
iris_scheduler_manager_balance (void)
{
}

/**
 * iris_scheduler_manager_unprepare:
 * @scheduler: An #IrisScheduler
 *
 * Unprepares a scheduler by removing all of its active threads
 * and resources.  The unused threads can then be repurposed to
 * other schedulers within the system.
 */
void
iris_scheduler_manager_unprepare (IrisScheduler *scheduler)
{
}
