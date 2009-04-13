/* iris-types.h
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

#ifndef __IRIS_TYPES_H__
#define __IRIS_TYPES_H__

#include <glib.h>

typedef enum
{
	IRIS_RECEIVE_NOW,
	IRIS_RECEIVE_LATER,
	IRIS_RECEIVE_NEVER
} IrisReceiveDecision;


typedef enum
{
	IRIS_DELIVERY_ACCEPTED          = 1,
	IRIS_DELIVERY_ACCEPTED_PAUSE    = 2,
	IRIS_DELIVERY_ACCEPTED_REMOVE   = 3,
	IRIS_DELIVERY_PAUSE             = 4,
	IRIS_DELIVERY_REMOVE            = 5
} IrisDeliveryStatus;

typedef struct _IrisReceiver         IrisReceiver;
typedef struct _IrisReceiverClass    IrisReceiverClass;
typedef struct _IrisReceiverPrivate  IrisReceiverPrivate;

typedef struct _IrisPort             IrisPort;
typedef struct _IrisPortClass        IrisPortClass;
typedef struct _IrisPortPrivate      IrisPortPrivate;

typedef struct _IrisArbiter          IrisArbiter;
typedef struct _IrisArbiterClass     IrisArbiterClass;
typedef struct _IrisArbiterPrivate   IrisArbiterPrivate;

typedef struct _IrisScheduler        IrisScheduler;
typedef struct _IrisSchedulerClass   IrisSchedulerClass;
typedef struct _IrisSchedulerPrivate IrisSchedulerPrivate;
typedef struct _IrisWSScheduler        IrisWSScheduler;
typedef struct _IrisWSSchedulerClass   IrisWSSchedulerClass;
typedef struct _IrisWSSchedulerPrivate IrisWSSchedulerPrivate;
typedef struct _IrisLFScheduler        IrisLFScheduler;
typedef struct _IrisLFSchedulerClass   IrisLFSchedulerClass;
typedef struct _IrisLFSchedulerPrivate IrisLFSchedulerPrivate;

typedef struct _IrisThread           IrisThread;
typedef struct _IrisThreadWork       IrisThreadWork;

typedef struct _IrisMessage          IrisMessage;

typedef struct _IrisStack            IrisStack;

typedef struct _IrisFreeList         IrisFreeList;

typedef struct _IrisLink             IrisLink;

typedef struct _IrisQueue            IrisQueue;
typedef struct _IrisQueueVTable      IrisQueueVTable;
typedef struct _IrisLFQueue          IrisLFQueue;
typedef struct _IrisWSQueue          IrisWSQueue;

typedef struct _IrisRRobin           IrisRRobin;

typedef void     (*IrisCallback)          (gpointer data);
typedef void     (*IrisMessageHandler)    (IrisMessage *message, gpointer data);
typedef void     (*IrisRRobinFunc)        (gpointer data, gpointer user_data);
typedef gboolean (*IrisRRobinForeachFunc) (IrisRRobin *rrobin, gpointer data, gpointer user_data);

#endif /* __IRIS_TYPES_H__ */
