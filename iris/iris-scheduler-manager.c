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

#include <glib/gprintf.h>

#include "iris-scheduler-manager.h"
#include "iris-thread.h"

#define THREAD_KEY ("__iris_threads")

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
 * @thread: The thread to yield
 *
 * Will yields a thread back to a scheduler.
 */
void
iris_scheduler_manager_yield (IrisThread *thread)
{
	g_return_if_fail (thread->scheduler != NULL);

	/* Remove the thread from the scheduler. */
	iris_scheduler_remove_thread (thread->scheduler, thread);

	/* We know that the threads scheduler definitely is no longer
	 * maxed out since this thread is ending.
	 */
	g_atomic_int_set (&thread->scheduler->maxed, FALSE);
	g_atomic_pointer_set (&thread->scheduler, NULL);
}

/**
 * get_or_create_thread_unlocked:
 * @exclusive: if the thread should try to yield when done processing
 *
 * Tries to first retreive a thread from the free thread list.  If that
 * fails, then a new thread is created.  If @exclusive, then the thread
 * will stay attached to the scheduler for the life of the scheduler.
 *
 * See iris_thread_manage()
 *
 * Return value: A re-purposed or new IrisThread
 */
static IrisThread*
get_or_create_thread_unlocked (gboolean exclusive)
{
	IrisThread *thread = NULL;

	if (singleton->free_list) {
		thread = singleton->free_list->data;
		singleton->free_list = g_list_remove (singleton->free_list,
		                                      singleton->free_list);
	}

	if (!thread) {
		thread = iris_thread_new (exclusive);

		/* FIXME: Add proper error handling. It's probably possible
		 *        that the system could deny creating us a new thread
		 *        at some point. I don't know what the limit is
		 *        currently on some architectures and os versions.
		 */
		g_assert (thread != NULL);
		g_assert (thread->thread != NULL);

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

	G_LOCK (singleton);

	for (i = 0; i < min_threads; i++) {
		thread = get_or_create_thread_unlocked (TRUE);
		g_assert (thread != NULL);

		/* Add proper error handling */
		g_return_if_fail (thread != NULL);

		thread->scheduler = scheduler;
		iris_scheduler_add_thread (scheduler, thread);
	}

	g_object_set_data (G_OBJECT (scheduler), THREAD_KEY,
	                   GINT_TO_POINTER (min_threads));

	G_UNLOCK (singleton);
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

/**
 * iris_scheduler_manager_request:
 * @scheduler: An #IrisScheduler
 * @per_quantum: The number of items processed in last quantum
 * @total: the total number of work items left
 *
 * Request that more workers be added to a scheduler. If @per_quantum
 * is > 0, then it will be used to try to maximize the number of threads
 * that can be added to minimize the time to process the queue.
 */
void
iris_scheduler_manager_request (IrisScheduler *scheduler,
                                guint          per_quantum,
                                guint          total)
{
	IrisThread *thread      = NULL;
	gint        requested   = 0;
	guint       n_threads   = 0;
	guint       max_threads = 0;
	gint        i;

	g_return_if_fail (scheduler != NULL);

	/* Only continue if the scheduler is not maxed out */
	if (g_atomic_int_get (&scheduler->maxed))
		return;

	if (!per_quantum)
		per_quantum = 1;

	requested = MAX (total / per_quantum, 1);
	max_threads = iris_scheduler_get_max_threads (scheduler);
	requested = MIN (requested, max_threads);

	G_LOCK (singleton);

	n_threads = GPOINTER_TO_INT (
		g_object_get_data (G_OBJECT (scheduler),
		                   THREAD_KEY));

	if (n_threads <= max_threads) {
		for (i = n_threads; i < requested; i++) {
			thread = get_or_create_thread_unlocked (FALSE);
			iris_scheduler_add_thread (scheduler, thread);
			n_threads++;
		}
	}

	g_object_set_data (G_OBJECT (scheduler), THREAD_KEY,
	                   GINT_TO_POINTER (n_threads));

	G_UNLOCK (singleton);
}

/**
 * iris_scheduler_manager_print_stat:
 *
 * Prints out information on the threads within iris to standard error.
 */
void
iris_scheduler_manager_print_stat (void)
{
	GList *iter;

	g_fprintf (stderr,
	           "\n    Iris Thread Status\n"
	           "  ============================================================\n");

	if (!singleton) {
		g_fprintf (stderr, "    No iris threads are currently active\n");
		return;
	}

	G_LOCK (singleton);

	for (iter = singleton->all_list; iter; iter = iter->next)
		iris_thread_print_stat (iter->data);

	G_UNLOCK (singleton);

	g_fprintf (stderr, "\n");
}
