/* iris-debug.h
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

#ifndef __IRIS_DEBUG_H__
#define __IRIS_DEBUG_H__

#include <glib.h>

typedef enum
{
	IRIS_DEBUG_SECTION_NONE        = 0,
	IRIS_DEBUG_SECTION_MESSAGE     = 1 << 1,
	IRIS_DEBUG_SECTION_PORT        = 1 << 2,
	IRIS_DEBUG_SECTION_RECEIVER    = 1 << 3,
	IRIS_DEBUG_SECTION_ARBITER     = 1 << 4,
	IRIS_DEBUG_SECTION_SCHEDULER   = 1 << 5,
	IRIS_DEBUG_SECTION_THREAD      = 1 << 6,
	IRIS_DEBUG_SECTION_TASK        = 1 << 7,
	IRIS_DEBUG_SECTION_QUEUE       = 1 << 8,
	IRIS_DEBUG_SECTION_STACK       = 1 << 9,
	IRIS_DEBUG_SECTION_RROBIN      = 1 << 10,
} IrisDebugSection;

#define IRIS_DEBUG_MESSAGE   IRIS_DEBUG_SECTION_MESSAGE,   __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_PORT      IRIS_DEBUG_SECTION_PORT,      __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_RECEIVER  IRIS_DEBUG_SECTION_RECEIVER,  __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_ARBITER   IRIS_DEBUG_SECTION_ARBITER,   __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_SCHEDULER IRIS_DEBUG_SECTION_SCHEDULER, __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_THREAD    IRIS_DEBUG_SECTION_THREAD,    __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_TASK      IRIS_DEBUG_SECTION_TASK,      __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_QUEUE     IRIS_DEBUG_SECTION_QUEUE,     __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_STACK     IRIS_DEBUG_SECTION_STACK,     __FILE__, __LINE__, G_STRFUNC
#define IRIS_DEBUG_RROBIN    IRIS_DEBUG_SECTION_RROBIN,    __FILE__, __LINE__, G_STRFUNC

void iris_debug_init        (void);
void iris_debug_init_thread (void);
void iris_debug             (IrisDebugSection  section,
                             const gchar      *file,
                             gint              line,
                             const gchar      *function);
void iris_debug_message     (IrisDebugSection  section,
                             const gchar      *file,
                             gint              line,
                             const gchar      *function,
                             const gchar      *format, ...) G_GNUC_PRINTF(5,6);

#endif /* __IRIS_DEBUG_H__ */
