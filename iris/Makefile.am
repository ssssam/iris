NULL =

BUILT_SOURCES = iris-version.h

sources_public_h =					\
	$(top_srcdir)/iris/iris-arbiter.h		\
	$(top_srcdir)/iris/iris-message.h		\
	$(top_srcdir)/iris/iris-port.h			\
	$(top_srcdir)/iris/iris-receiver.h		\
	$(top_srcdir)/iris/iris-scheduler.h		\
	$(top_srcdir)/iris/iris-scheduler-manager.h	\
	$(top_srcdir)/iris/iris-task.h			\
	$(NULL)

sources_private_h =					\
	$(NULL)

sources_c = 						\
	iris-arbiter.c					\
	iris-message.c					\
	iris-port.c					\
	iris-receiver.c					\
	iris-scheduler.c				\
	iris-scheduler-manager.c			\
	iris-task.c					\
	$(NULL)

irisincludedir = $(includedir)/iris-$(IRIS_API_VERSION)/iris
irisinclude_DATA = \
	$(sources_public_h) 				\
	$(top_srcdir)/iris/iris.h 			\
	$(NULL)

lib_LTLIBRARIES = libiris-@IRIS_API_VERSION@.la

libiris_@IRIS_API_VERSION@_la_SOURCES = 		\
	$(sources_public_h) 				\
	$(sources_private_h) 				\
	$(sources_c) 					\
	$(BUILT_SOURCES) 				\
	iris.h						\
	$(NULL)

libiris_@IRIS_API_VERSION@_la_CFLAGS =			\
	-I$(top_srcdir)					\
	-DPREFIX=\""$(prefix)"\"			\
	-DLIBDIR=\""$(libdir)"\"			\
	-DG_LOG_DOMAIN=\"Iris\"				\
	-DG_DISABLE_DEPRECATED				\
	-DIRIS_COMPILATION				\
	$(IRIS_MAINTAINER_CFLAGS)			\
	$(IRIS_DEBUG_CFLAGS)				\
	$(IRIS_CFLAGS)					\
	$(NULL)

libiris_@IRIS_API_VERSION@_la_LDFLAGS =			\
	$(IRIS_GLIB_LT_LDFLAGS) 			\
	-export-dynamic 				\
	-export-symbols-regex "^iris.*" 		\
	-rpath $(libdir) 				\
	$(NULL)

libiris_@IRIS_API_VERSION@_la_LIBADD = $(IRIS_LIBS)

DISTCLEANFILES = 					\
	iris-version.h					\
	$(NULL)

EXTRA_DIST =						\
	iris-version.h.in				\
	iris.h						\
	$(NULL)

