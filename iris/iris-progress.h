/* iris-progress.h
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

#ifndef __IRIS_PROGRESS_H__
#define __IRIS_PROGRESS_H__

/**
 * IrisProgressMessageType:
 * @IRIS_PROGRESS_MESSAGE_COMPLETE: the task has completed.
 * @IRIS_PROGRESS_MESSAGE_CANCELED: the task was canceled.
 * @IRIS_PROGRESS_MESSAGE_PULSE: sent when it's not possible to calculate
 *                               actual progress. Watch must have
 *                               @IRIS_PROGRESS_MONITOR_ACTIVITY_ONLY display
 *                               style.
 * @IRIS_PROGRESS_MESSAGE_FRACTION: should contain a float between 0 and 1.
 *                                  Sent when progress isn't best represented
 *                                  as discrite items. Watch must not have
 *                                  @IRIS_PROGRESS_MONITOR_ITEMS display style.
 * @IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS: integer; number of items completed.
 * @IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS: integer; sent when new items are enqueued.
 *                                     Note that there is no need to send
 *                                     @IRIS_PROGRESS_MESSAGE_FRACTION if item
 *                                     counts are being sent.
 * @IRIS_PROGRESS_MESSAGE_TITLE: string; sent when the title of the process changes
 *
 * An #IrisProgressMonitor listens for these messages to update its UI. It's
 * recommended you don't send status messages more than once every 250ms or so;
 * there's no point.
 *
 * No messages should be sent after IRIS_PROGRESS_MESSAGE_CANCELED or
 * IRIS_PROGRESS_MESSAGE_COMPLETE.
 *
 * FIXME: is this still TRUE?
 * The same message could be sent more than once; any listeners must be able to
 * handle for example @IRIS_PROGRESS_MESSAGE_COMPLETE being received twice.
 **/
typedef enum
{
	/* Control */
	IRIS_PROGRESS_MESSAGE_COMPLETE,
	IRIS_PROGRESS_MESSAGE_CANCELED,

	/* Progress */
	IRIS_PROGRESS_MESSAGE_PULSE,
	IRIS_PROGRESS_MESSAGE_FRACTION,
	IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS,
	IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS,

	/* Display */
	IRIS_PROGRESS_MESSAGE_TITLE
} IrisProgressMessageType;

#endif
