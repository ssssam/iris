include $(top_srcdir)/Makefile.decl

NULL = 
	
noinst_PROGRAMS =		\
	arbiter-1		\
	coordination-arbiter-1	\
	free-list-1		\
	gdestructiblepointer-1 \
	gmainscheduler-1	\
	gstamppointer-1		\
	lf-queue-1		\
	message-1		\
	port-1			\
	process-1		\
	process-2		\
	queue-1			\
	receiver-1		\
	receiver-scheduler-1	\
	rrobin-1		\
	scheduler-manager-1	\
	scheduler-1		\
	scheduler-2		\
	service-1		\
	stack-1			\
	task-1			\
	thread-1		\
	ws-queue-1

TEST_PROGS +=			\
	arbiter-1		\
	coordination-arbiter-1	\
	free-list-1		\
	gdestructiblepointer-1 \
	gmainscheduler-1	\
	gstamppointer-1		\
	lf-queue-1		\
	message-1		\
	port-1			\
	process-1		\
	process-2		\
	queue-1			\
	receiver-1		\
	receiver-scheduler-1	\
	rrobin-1		\
	scheduler-manager-1	\
	scheduler-1		\
	scheduler-2		\
	service-1		\
	stack-1			\
	task-1			\
	thread-1		\
	ws-queue-1

if ENABLE_GTK
noinst_PROGRAMS +=		\
	progress-dialog-gtk-1	\
	progress-monitor-gtk-1

TEST_PROGS += 		\
	progress-dialog-gtk-1	\
	progress-monitor-gtk-1
endif

noinst_PROGRAMS += $(NULL)
TEST_PROGS += $(NULL)

INCLUDES = -I$(top_srcdir)/iris
LDADD = $(top_builddir)/iris/libiris-1.0.la

AM_CFLAGS = $(IRIS_CFLAGS)
AM_LDFLAGS = $(IRIS_LIBS)

if ENABLE_GTK
INCLUDES += -I$(top_srcdir)/iris-gtk
LDADD += $(top_builddir)/iris-gtk/libiris-gtk-1.0.la

AM_CFLAGS += $(GTK_CFLAGS)
AM_LDFLAGS += $(GTK_LIBS)
endif

arbiter_1_sources = arbiter-1.c
gdestructiblepointer_1_sources = gdestructiblepointer-1.c
message_1_sources = message-1.c
port_1_sources = port-1.c mocks/mock-callback-receiver.c
process_1_sources = process-1.c
process_2_sources = process-2.c
receiver_1_sources = receiver-1.c
scheduler_manager_1_sources = scheduler-manager-1.c
scheduler_1_sources = scheduler-1.c
scheduler_2_sources = scheduler-2.c
free_list_1_sources = free-list-1.c
stack_1_sources = stack-1.c
queue_1_sources = queue-1.c
lf_queue_1_sources = lf-queue-1.c
ws_queue_1_sources = ws-queue-1.c
task_1_sources = task-1.c
thread_1_sources = thread-1.c
rrobin_1_sources = rrobin-1.c
gstamppointer_1_sources = gstamppointer-1.c
coordination_arbiter_1_sources = coordination-arbiter-1.c
service_1_sources = service-1.c
gmainscheduler_1_sources = gmainscheduler-1.c
receiver_scheduler_1_sources = receiver-scheduler-1.c

progress_monitor_gtk_1_sources = progress-monitor-gtk-1.c
progress_dialog_gtk_1_sources = progress-dialog-gtk-1.c

EXTRA_DIST +=					\
	mocks/mock-callback-receiver.c		\
	mocks/mock-callback-receiver.h		\
	mocks/mock-scheduler.h			\
	mocks/mock-service.h			\
	$(NULL)

