/* irismodule.c
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

#ifndef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <Python.h>
#include <pygobject.h>
#include "iris/iris.h"

void pyiris_register_classes (PyObject    *d);

extern PyMethodDef pyiris_functions[];

DL_EXPORT (void)
initiris (void)
{
	PyObject *m, *d;

	PyEval_InitThreads ();
	init_pygobject ();
	pyg_enable_threads ();
	iris_init ();

	m = Py_InitModule ("iris", pyiris_functions);
	d = PyModule_GetDict (m);

	pyiris_register_classes (d);

	if (PyErr_Occurred ())
		Py_FatalError ("Error initializing module iris");
}
