/* iris-atomics.c
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

#include "iris-atomics.h"

#if DARWIN
#include <libkern/OSAtomic.h>
#elif defined(WIN32)
#include <windows.h>
#endif

inline gint
iris_atomics_fetch_and_inc (volatile void *ptr)
{
#if DARWIN
	return OSAtomicAdd32 (1, ptr) - 1;
#elif defined(WIN32)
	return InterlockedIncrement (ptr) - 1;
#else
	return __sync_fetch_and_add ((gint*)ptr, 1);
#endif
}
