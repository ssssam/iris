/* iris-process.h
 *
 * Copyright (C) 2009-11 Sam Thursfield <ssssam@gmail.com>
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

#ifndef __IRIS_PROCESS_H__
#define __IRIS_PROCESS_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "iris-message.h"
#include "iris-port.h"
#include "iris-task.h"


G_BEGIN_DECLS

#define IRIS_TYPE_PROCESS            (iris_process_get_type ())
#define IRIS_PROCESS(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_PROCESS, IrisProcess))
#define IRIS_PROCESS_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_PROCESS, IrisProcess const))
#define IRIS_PROCESS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  IRIS_TYPE_PROCESS, IrisProcessClass))
#define IRIS_IS_PROCESS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_PROCESS))
#define IRIS_IS_PROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  IRIS_TYPE_PROCESS))
#define IRIS_PROCESS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  IRIS_TYPE_PROCESS, IrisProcessClass))

typedef struct _IrisProcess        IrisProcess;
typedef struct _IrisProcessClass   IrisProcessClass;
typedef struct _IrisProcessPrivate IrisProcessPrivate;

/**
 * IrisProcessFunc:
 * @process: An #IrisProcess
 * @work_item: An #IrisMessage
 * @user_data: user specified data
 *
 * Callback for processes, to handle one work item.
 */
typedef void (*IrisProcessFunc) (IrisProcess *process, IrisMessage *work_item, gpointer user_data);

struct _IrisProcess
{
	IrisTask parent;

	/*< private >*/
	IrisProcessPrivate *priv;
};

struct _IrisProcessClass
{
	IrisTaskClass parent_class;

	void     (*post_work_item)      (IrisProcess *process, IrisMessage *message);

	void     (*reserved1)           (void);
	void     (*reserved2)           (void);
	void     (*reserved3)           (void);
	void     (*reserved4)           (void);
};

GType         iris_process_get_type              (void) G_GNUC_CONST;

IrisProcess*  iris_process_new                   (void);
IrisProcess*  iris_process_new_with_func         (IrisProcessFunc      func,
                                                  gpointer             user_data,
                                                  GDestroyNotify       notify);
IrisProcess*  iris_process_new_with_closure      (GClosure            *closure);

void          iris_process_run                   (IrisProcess            *process);
void          iris_process_cancel                (IrisProcess            *process);

void          iris_process_enqueue               (IrisProcess            *process,
                                                  IrisMessage            *work_item);
void          iris_process_no_more_work          (IrisProcess            *process);

void          iris_process_connect               (IrisProcess            *head,
                                                  IrisProcess            *tail);
void          iris_process_forward               (IrisProcess            *process,
                                                  IrisMessage            *work_item);
void          iris_process_recurse               (IrisProcess            *process,
                                                  IrisMessage            *work_item);

gboolean      iris_process_is_executing          (IrisProcess            *process);
gboolean      iris_process_is_finished           (IrisProcess            *process);
gboolean      iris_process_has_succeeded         (IrisProcess            *process);
gboolean      iris_process_was_canceled          (IrisProcess            *process);

gboolean      iris_process_has_predecessor       (IrisProcess            *process);
gboolean      iris_process_has_successor         (IrisProcess            *process);

IrisProcess*  iris_process_get_predecessor       (IrisProcess            *process);
IrisProcess*  iris_process_get_successor         (IrisProcess            *process);
const gchar*  iris_process_get_title             (IrisProcess            *process);
void          iris_process_get_status            (IrisProcess            *process,
                                                  gint                   *p_processed_items,
                                                  gint                   *p_total_items);
gint          iris_process_get_queue_length      (IrisProcess            *process);

void          iris_process_set_func              (IrisProcess            *process,
                                                  IrisProcessFunc         func,
                                                  gpointer                user_data,
                                                  GDestroyNotify          notify);
void          iris_process_set_closure           (IrisProcess            *process,
                                                  GClosure               *closure);
void          iris_process_set_title             (IrisProcess            *process,
                                                  const gchar            *title);
void          iris_process_set_output_estimation (IrisProcess            *process,
                                                  gfloat                  factor);

void          iris_process_add_watch             (IrisProcess            *process,
                                                  IrisPort               *watch_port);

G_END_DECLS

#endif /* __IRIS_PROCESS_H__ */
