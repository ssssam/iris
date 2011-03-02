#include <glib.h>
#include <iris/iris.h>
#include <iris/iris-scheduler-private.h>

#define MOCK_SCHEDULER_TYPE   (mock_scheduler_get_type())

typedef struct {
	IrisScheduler parent;
} MockScheduler;

typedef struct {
	IrisSchedulerClass parent;
} MockSchedulerClass;

G_DEFINE_TYPE (MockScheduler, mock_scheduler, IRIS_TYPE_SCHEDULER);

IrisScheduler* mock_scheduler_new (void);

static void
queue_sync (IrisScheduler  *scheduler,
            IrisCallback    func,
            gpointer        data,
            GDestroyNotify  notify)
{
	g_return_if_fail (IRIS_IS_SCHEDULER (scheduler));
	g_return_if_fail (func != NULL);

	func (data);

	if (notify)
		notify (data);
}

static void
foreach (IrisScheduler            *scheduler,
         IrisSchedulerForeachFunc  func,
         gpointer                  user_data) {
	return;
}

static void
mock_scheduler_class_init (MockSchedulerClass *klass)
{
	IRIS_SCHEDULER_CLASS (klass)->queue = queue_sync;
	IRIS_SCHEDULER_CLASS (klass)->foreach = foreach;
}

static void
mock_scheduler_init (MockScheduler *mock_scheduler)
{
	/* Avoid being given threads by the scheduler manager */
	IRIS_SCHEDULER (mock_scheduler)->priv->initialized = TRUE;
}


IrisScheduler*
mock_scheduler_new (void)
{
	return g_object_new (MOCK_SCHEDULER_TYPE, NULL);
}
