include $(top_srcdir)/Makefile.decl

NULL = 
	
noinst_PROGRAMS =		\
	arbiter-1		\
	coordination-arbiter-1	\
	free-list-1		\
	gstamppointer-1		\
	lf-queue-1		\
	message-1		\
	port-1			\
	queue-1			\
	receiver-1		\
	rrobin-1		\
	scheduler-manager-1	\
	scheduler-1		\
	service-1		\
	stack-1			\
	task-1			\
	thread-1		\
	ws-queue-1		\
	$(NULL)

TEST_PROGS +=			\
	arbiter-1		\
	coordination-arbiter-1	\
	free-list-1		\
	gstamppointer-1		\
	lf-queue-1		\
	message-1		\
	port-1			\
	queue-1			\
	receiver-1		\
	rrobin-1		\
	scheduler-manager-1	\
	scheduler-1		\
	service-1		\
	stack-1			\
	task-1			\
	thread-1		\
	ws-queue-1		\
	$(NULL)

INCLUDES = -I$(top_srcdir)
LDADD = $(top_builddir)/iris/libiris-1.0.la

AM_CFLAGS = $(IRIS_CFLAGS)
AM_LDFLAGS = $(IRIS_LIBS)

arbiter_1_sources = arbiter-1.c
message_1_sources = message-1.c
port_1_sources = port-1.c mocks/mock-callback-receiver.c
receiver_1_sources = receiver-1.c
scheduler_manager_1_sources = scheduler-manager-1.c
scheduler_1_sources = scheduler-1.c
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

EXTRA_DIST =					\
	mocks/mock-callback-receiver.c		\
	mocks/mock-callback-receiver.h		\
	mocks/mock-scheduler.h			\
	mocks/mock-service.h			\
	$(NULL)

