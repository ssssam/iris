/* iris-stack.h
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

#ifndef __IRIS_STACK_H__
#define __IRIS_STACK_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define IRIS_TYPE_STACK (iris_stack_get_type())

typedef struct _IrisStack IrisStack;

GType      iris_stack_get_type (void) G_GNUC_CONST;
IrisStack* iris_stack_new      (void);
void       iris_stack_push     (IrisStack *stack, gpointer data);
gpointer   iris_stack_pop      (IrisStack *stack);
IrisStack* iris_stack_ref      (IrisStack *stack);
void       iris_stack_unref    (IrisStack *stack);

G_END_DECLS

#endif /* __IRIS_STACK_H__ */
