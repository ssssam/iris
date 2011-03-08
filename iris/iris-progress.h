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
 * IrisProgressMode
 * @IRIS_PROGRESS_ACTIVITY_ONLY: task cannot predict how long it needs to
 *   execute. The progress monitor will not display progress, just a block
 *   bouncing back and forth inside the progress bar to imply activity.
 * @IRIS_PROGRESS_CONTINUOUS: work is best expressed as "x% complete"
 * @IRIS_PROGRESS_DISCRETE: task can be displayed in terms of items. This is
 *   the default progress mode for #IrisProcess objects.
 *
 * These values instruct progress monitor widgets to display the progress of a
 * task or process in a specific way.
 *
 * The display style of an #IrisProgressGroup is based on its children. While
 * any watches are @IRIS_PROGRESS_MONITOR_ACTIVITY_ONLY and are not complete,
 * the group will display in activity mode. Otherwise, it will display as a
 * percentage.
 */
typedef enum
{
	IRIS_PROGRESS_ACTIVITY_ONLY,
	IRIS_PROGRESS_CONTINUOUS,
	IRIS_PROGRESS_DISCRETE
} IrisProgressMode;

/**
 * IrisProgressMessageType:
 * @IRIS_PROGRESS_MESSAGE_COMPLETE: the task has completed.
 * @IRIS_PROGRESS_MESSAGE_CANCELED: the task was canceled.
 * @IRIS_PROGRESS_MESSAGE_PULSE: sent for tasks in
 *                               %IRIS_PROGRESS_ACTIVITY_ONLY progress mode.
 * @IRIS_PROGRESS_MESSAGE_FRACTION: should contain a float between 0 and 1.
 *                                  Sent for tasks in
 *                                  %IRIS_PROGRESS_CONTINUOUS mode.
 * @IRIS_PROGRESS_MESSAGE_PROCESSED_ITEMS: integer; number of items completed.
 * @IRIS_PROGRESS_MESSAGE_TOTAL_ITEMS: integer; sent when new items are enqueued.
 *                                     These two are for %IRIS_PROGRESS_DISCRETE
 *                                     tasks.
 * @IRIS_PROGRESS_MESSAGE_TITLE: string; sent when the title of the process changes
 *
 * An #IrisProgressMonitor listens for these messages to update its UI. It's
 * recommended you don't send status messages more than once every 200ms or so;
 * there's no point.
 *
 * Only one of %IRIS_PROGRESS_MESSAGE_CANCELED or
 * %IRIS_PROGRESS_MESSAGE_COMPLETE should ever be sent, and no more messages can
 * be sent after them.
 **/
typedef enum
{
	/* Control */
	IRIS_PROGRESS_MESSAGE_COMPLETE = 1,
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
