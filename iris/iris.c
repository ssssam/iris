#include <glib.h>

#include "iris.h"
#include "iris-debug.h"

G_LOCK_DEFINE (initialized);
static volatile gboolean initialized = FALSE;

void
iris_init (void)
{
	G_LOCK (initialized);

	if (!initialized) {
		if (!g_thread_supported ())
			g_thread_init (NULL);
		g_type_init ();
		iris_debug_init ();
	}

	G_UNLOCK (initialized);
}
