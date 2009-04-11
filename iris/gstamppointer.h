/* gstamppointer.h
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

#ifndef __G_STAMP_POINTER_H__
#define __G_STAMP_POINTER_H__

/* #gstamppointer is a pointer that uses the lower 2 bits of the pointer
 * for a stamp. The stamp is a simple counter that can be incremented
 * repeatedly, and will roll over.
 *
 * This is used to help mitigate the ABA problem that is common in
 * lock-free data structures. However, if the ABA problem occurs 4 times
 * repeatedly during the 1st threads pre-emption, the problem still exists.
 *
 * This requires that the destination of the pointer is aligned to at least
 * a 32bit integer such as sizeof(void*). Our pointers used for link->next
 * are more IrisLink nodes, which are allocated by the slice allocator.
 * The GSlice allocator provides alignment to sizeof(void*).
 */

typedef gpointer gstamppointer;

#define G_STAMP_POINTER(p)             ((gstamppointer)p)
#define G_STAMP_POINTER_GET_POINTER(p) ((gstamppointer)(((gulong)p) & ((gulong)~0x03)))
#define G_STAMP_POINTER_GET_STAMP(p)   ((gulong)       (((gulong)p) & ((gulong) 0x03)))
#define G_STAMP_POINTER_INCREMENT(p)   ((gstamppointer)        \
        (((gulong)((G_STAMP_POINTER_GET_STAMP(p) + 1) & 0x03)) \
        |((gulong)(G_STAMP_POINTER_GET_POINTER(p)))))
#define G_STAMP_POINTER_GET_LINK(p)    ((IrisLink*)G_STAMP_POINTER_GET_POINTER(p))

#endif /* __G_STAMP_POINTER_H__ */
