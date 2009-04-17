/* iris-any-task.c
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

#include "iris-task.h"
#include "iris-task-private.h"

#define IRIS_TYPE_ANY_TASK		(iris_any_task_get_type ())
#define IRIS_ANY_TASK(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_ANY_TASK, IrisAnyTask))
#define IRIS_ANY_TASK_CONST(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_ANY_TASK, IrisAnyTask const))
#define IRIS_ANY_TASK_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_ANY_TASK, IrisAnyTaskClass))
#define IRIS_IS_ANY_TASK(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_ANY_TASK))
#define IRIS_IS_ANY_TASK_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_ANY_TASK))
#define IRIS_ANY_TASK_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_ANY_TASK, IrisAnyTaskClass))

typedef struct _IrisAnyTask		IrisAnyTask;
typedef struct _IrisAnyTaskClass	IrisAnyTaskClass;
typedef struct _IrisAnyTaskPrivate	IrisAnyTaskPrivate;

struct _IrisAnyTask {
	IrisTask parent;
	
	IrisAnyTaskPrivate *priv;
};

struct _IrisAnyTaskClass {
	IrisTaskClass parent_class;
};

GType        iris_any_task_get_type (void) G_GNUC_CONST;
IrisAnyTask* iris_any_task_new      (void);

G_DEFINE_TYPE (IrisAnyTask, iris_any_task, IRIS_TYPE_TASK);

static void
iris_any_task_dependency_canceled_real (IrisTask *task,
                                        IrisTask *dep)
{
	IrisTaskPrivate *priv;

	priv = task->priv;

	if (g_list_length (priv->dependencies) == 1 && g_list_find (priv->dependencies, dep)) {
		g_list_foreach (priv->dependencies, (GFunc)g_object_unref, NULL);
		g_list_free (priv->dependencies);
		priv->dependencies = NULL;
		/* cancel if this is our last option and it was canceled */
		iris_task_cancel (task);
		return;
	}

	priv->dependencies = g_list_remove (priv->dependencies, dep);
}

static void
iris_any_task_dependency_finished_real (IrisTask *task,
                                        IrisTask *dep)
{
	IrisTaskPrivate *priv;

	priv = task->priv;

	dep = g_object_ref (dep);

	if (g_list_find (priv->dependencies, dep)) {
		/* none of the other tasks have finished yet it seems */
		g_list_foreach (priv->dependencies, (GFunc)g_object_unref, NULL);
		g_list_free (priv->dependencies);
		priv->dependencies = g_list_append (NULL, dep);
		iris_task_remove_dependency_sync (task, dep);
	}
}

static void
iris_any_task_class_init (IrisAnyTaskClass *any_task_class)
{
	IrisTaskClass *task_class;

	task_class = IRIS_TASK_CLASS (any_task_class);
	task_class->dependency_finished = iris_any_task_dependency_finished_real;
	task_class->dependency_canceled = iris_any_task_dependency_canceled_real;
}

static void
iris_any_task_init (IrisAnyTask *task)
{
}

static void
iris_any_task_dummy (IrisTask *task,
                     gpointer  user_data)
{
}

IrisTask*
iris_task_any_of (IrisTask *first_task, ...)
{
	IrisTask *task;
	IrisTask *iter;
	GClosure *closure;
	va_list   args;

	if (first_task == NULL)
		return NULL;

	closure = g_cclosure_new (G_CALLBACK (iris_any_task_dummy), NULL, NULL);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__VOID);
	task = g_object_new (IRIS_TYPE_ANY_TASK, NULL);
	task->priv->closure = g_closure_ref (closure);
	g_closure_unref (closure);

	iter = first_task;

	va_start (args, first_task);

	while (iter) {
		if (IRIS_IS_TASK (iter))
			iris_task_add_dependency (task, iter);
		iter = va_arg (args, IrisTask*);
	}

	va_end (args);

	return task;
}
