/* iris-progress.h
 *
 * Copyright (C) 2009 Sam Thursfield <ssssam@gmail.com>
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

#ifndef __IRIS_PROGRESS_H__
#define __IRIS_PROGRESS_H__

/**
 * IrisProgressMessageType:
 * @IRIS_PROGRESS_MESSAGE_COMPLETE: the task has completed
 * @IRIS_PROGRESS_MESSAGE_CANCELLED: the task was cancelled.
 * @IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS: integer; number of items completed.
 * @IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS: integer; send this when the queue grows.
 * @IRIS_PROGRESS_MESSAGE_FRACTION: should contain a float between 0 and 1. Only
 *                                  send this message if you are not sending the
 *                                  above two.
 *
 * An #IrisProgressMonitor listens for these messages to update its UI. It's
 * recommended you don't send status messages more than once every 250ms or so;
 * there's no point.
 **/
typedef enum
{
	IRIS_PROGRESS_MESSAGE_COMPLETE,
	IRIS_PROGRESS_MESSAGE_CANCELLED,
	IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS,
	IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS,
	IRIS_PROGRESS_MESSAGE_FRACTION
} IrisProgressMessageType;

#endif