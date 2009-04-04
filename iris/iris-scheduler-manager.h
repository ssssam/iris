/* iris-scheduler-manager.h
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

#ifndef __IRIS_SCHEDULER_MANAGER_H__
#define __IRIS_SCHEDULER_MANAGER_H__

#include <glib-object.h>

#include "iris-scheduler.h"

G_BEGIN_DECLS

void iris_scheduler_manager_prepare    (IrisScheduler *scheduler);
void iris_scheduler_manager_unprepare  (IrisScheduler *scheduler);
void iris_scheduler_manager_balance    (void);
void iris_scheduler_manager_print_stat (void);

G_END_DECLS

#endif /* __IRIS_SCHEDULER_MANAGER_H__ */
