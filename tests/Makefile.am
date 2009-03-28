include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = message-1 port-1

TEST_PROGS += message-1 port-1

INCLUDES = -I$(top_srcdir)
LDADD = $(top_builddir)/iris/libiris-1.0.la

AM_CFLAGS = $(IRIS_CFLAGS)
AM_LDFLAGS = $(IRIS_LIBS)

message_1_sources = message-1.c
