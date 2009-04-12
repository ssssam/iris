include $(top_srcdir)/Makefile.decl
	
noinst_PROGRAMS =		\
	message-1		\
	port-1			\
	receiver-1		\
	scheduler-manager-1	\
	free-list-1		\
	stack-1			\
	queue-1			\
	lf-queue-1

TEST_PROGS +=			\
	message-1		\
	port-1			\
	receiver-1		\
	scheduler-manager-1	\
	free-list-1		\
	stack-1			\
	queue-1			\
	lf-queue-1

INCLUDES = -I$(top_srcdir)
LDADD = $(top_builddir)/iris/libiris-1.0.la

AM_CFLAGS = $(IRIS_CFLAGS)
AM_LDFLAGS = $(IRIS_LIBS)

message_1_sources = message-1.c
port_1_sources = port-1.c mocks/mock-callback-receiver.c
receiver_1_sources = receiver-1.c
scheduler_manager_1_sources = scheduler-manager-1.c
free_list_1_sources = free-list-1.c
stack_1_sources = stack-1.c
queue_1_sources = queue-1.c
lf_queue_q_sources = lf-queue-1.c
