/* iris.h
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

#ifndef __IRIS_H__
#define __IRIS_H__

/* basic data structures */
#include "iris-queue.h"
#include "iris-lfqueue.h"
#include "iris-wsqueue.h"
#include "iris-rrobin.h"
#include "iris-stack.h"

/* scheduler subsystem */
#include "iris-scheduler.h"
#include "iris-gmainscheduler.h"
#include "iris-wsscheduler.h"
#include "iris-scheduler-manager.h"

/* message passing and arbitration */
#include "iris-message.h"
#include "iris-receiver.h"
#include "iris-port.h"
#include "iris-arbiter.h"

/* high level abstractions */
#include "iris-service.h"
#include "iris-task.h"

/* global API methods */
void iris_init (void);

#endif /* __IRIS_H__ */
