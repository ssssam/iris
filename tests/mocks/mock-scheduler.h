#include <glib.h>
#include <iris/iris.h>
#include <iris/iris-scheduler-private.h>

IrisScheduler* mock_scheduler_new (void);

static void
queue_sync (IrisScheduler     *scheduler,
            IrisSchedulerFunc  func,
            gpointer           data,
            GDestroyNotify     notify)
{
	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));
	g_return_if_fail (func != NULL);

	func (data);

	if (notify)
		notify (data);
}

IrisScheduler*
mock_scheduler_new (void)
{
	IrisScheduler *sched;

	sched = iris_scheduler_new ();
	IRIS_SCHEDULER_GET_CLASS (sched)->queue = queue_sync;

	return sched;
}
