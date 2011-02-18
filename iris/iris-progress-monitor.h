/* iris-progress-monitor.h
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

#ifndef __IRIS_PROGRESS_MONITOR_H__
#define __IRIS_PROGRESS_MONITOR_H__

#include <glib-object.h>
#include <gio/gio.h>

#include "iris-port.h"
#include "iris-progress.h"
#include "iris-process.h"

G_BEGIN_DECLS

#define IRIS_TYPE_PROGRESS_MONITOR                (iris_progress_monitor_get_type ())
#define IRIS_PROGRESS_MONITOR(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), IRIS_TYPE_PROGRESS_MONITOR, IrisProgressMonitor))
#define IRIS_IS_PROGRESS_MONITOR(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), IRIS_TYPE_PROGRESS_MONITOR))
#define IRIS_PROGRESS_MONITOR_GET_INTERFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj),  IRIS_TYPE_PROGRESS_MONITOR, IrisProgressMonitorInterface))

typedef struct _IrisProgressMonitor            IrisProgressMonitor;
typedef struct _IrisProgressMonitorInterface   IrisProgressMonitorInterface;

typedef struct _IrisProgressWatch              IrisProgressWatch;
typedef struct _IrisProgressGroup              IrisProgressGroup;

/**
 * IrisProgressMonitorDisplayStyle:
 * @IRIS_PROGRESS_MONITOR_ITEMS: display "x items of y"
 * @IRIS_PROGRESS_MONITOR_PERCENTAGE: display "x% complete"
 *
 * These values instruct progress monitor widgets to display the progress of a
 * watch in a specific format.
 *
 **/
typedef enum {
	IRIS_PROGRESS_MONITOR_ITEMS,
	IRIS_PROGRESS_MONITOR_PERCENTAGE
} IrisProgressMonitorDisplayStyle;


struct _IrisProgressMonitorInterface
{
	GTypeInterface parent_iface;

	void     (*add_group)            (IrisProgressMonitor *progress_monitor,
	                                  IrisProgressGroup   *group);
	void     (*add_watch)            (IrisProgressMonitor *progress_monitor,
	                                  IrisProgressWatch   *watch);

	void     (*handle_message)       (IrisProgressMonitor *progress_monitor,
	                                  IrisProgressWatch   *watch,
	                                  IrisMessage         *message);

	gboolean (*is_watching_task)     (IrisProgressMonitor *progress_monitor,
	                                  IrisTask            *task);

	void     (*set_permanent_mode)   (IrisProgressMonitor *progress_monitor,
	                                  gboolean             enable);

	void     (*set_watch_hide_delay) (IrisProgressMonitor *progress_monitor,
	                                  gint                 millisecs);

	/* Private */
	void               (*remove_group)           (IrisProgressMonitor *progress_monitor,
	                                              IrisProgressGroup   *group);
	IrisProgressWatch *(*get_watch)              (IrisProgressMonitor *progress_monitor,
	                                              IrisTask            *task);
	void               (*remove_watch)           (IrisProgressMonitor *progress_monitor,
	                                              IrisProgressWatch   *watch,
	                                              gboolean             temporary);
	void               (*reorder_watch_in_group) (IrisProgressMonitor *progress_monitor,
	                                              IrisProgressWatch   *watch,
	                                              gboolean             at_end);

	void     (*reserved1)            (void);
	void     (*reserved2)            (void);
	void     (*reserved3)            (void);
	void     (*reserved4)            (void);
};

GType         iris_progress_monitor_get_type             (void) G_GNUC_CONST;

/* General API */
IrisProgressGroup *iris_progress_monitor_add_group       (IrisProgressMonitor *progress_monitor,
                                                          const gchar         *title,
                                                          const gchar         *plural);
IrisPort          *iris_progress_monitor_add_watch       (IrisProgressMonitor             *progress_monitor,
                                                          IrisTask                        *task,
                                                          const gchar                     *title,
                                                          IrisProgressMonitorDisplayStyle  display_style,
                                                          IrisProgressGroup               *group);

void          iris_progress_monitor_set_permanent_mode   (IrisProgressMonitor             *progress_monitor,
                                                          gboolean                         enable);

void          iris_progress_monitor_set_watch_hide_delay (IrisProgressMonitor             *progress_monitor,
                                                          gint                             milliseconds);

/* IrisProcess-specific API */
void          iris_progress_monitor_watch_process                (IrisProgressMonitor             *progress_monitor,
                                                                  IrisProcess                     *process,
                                                                  IrisProgressMonitorDisplayStyle  display_style,
                                                                  IrisProgressGroup               *group);
void          iris_progress_monitor_watch_process_chain          (IrisProgressMonitor             *progress_monitor,
                                                                  IrisProcess                     *process,
                                                                  IrisProgressMonitorDisplayStyle  display_style,
                                                                  const gchar                     *title,
                                                                  const gchar                     *plural);
void          iris_progress_monitor_watch_process_chain_in_group (IrisProgressMonitor             *progress_monitor,
                                                                  IrisProcess                     *process,
                                                                  IrisProgressMonitorDisplayStyle  display_style,
                                                                  IrisProgressGroup               *group);

void          iris_progress_group_ref   (IrisProgressGroup *progress_group);
void          iris_progress_group_unref (IrisProgressGroup *progress_group);

G_END_DECLS

#endif /* __IRIS_PROGRESS_MONITOR_H__ */
