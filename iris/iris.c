#include <glib.h>

#include "iris.h"
#include "iris-debug.h"

void
iris_init (void)
{
	if (!g_thread_supported ())
		g_thread_init (NULL);
	g_type_init ();
	iris_debug_init ();
}
