include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = message-1 port-1 receiver-1 scheduler-manager-1

TEST_PROGS += message-1 port-1 receiver-1 scheduler-manager-1

INCLUDES = -I$(top_srcdir)
LDADD = $(top_builddir)/iris/libiris-1.0.la

AM_CFLAGS = $(IRIS_CFLAGS)
AM_LDFLAGS = $(IRIS_LIBS)

message_1_sources = message-1.c
port_1_sources = port-1.c mocks/mock-callback-receiver.c
receiver_1_sources = receiver-1.c
scheduler_manager_1_sources = scheduler-manager-1.c
