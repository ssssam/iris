NULL =

BUILT_SOURCES = iris-version.h

sources_public_h =					\
	$(top_srcdir)/iris/iris-arbiter.h		\
	$(top_srcdir)/iris/iris-free-list.h		\
	$(top_srcdir)/iris/iris-link.h			\
	$(top_srcdir)/iris/iris-lfqueue.h		\
	$(top_srcdir)/iris/iris-lfscheduler.h		\
	$(top_srcdir)/iris/iris-message.h		\
	$(top_srcdir)/iris/iris-port.h			\
	$(top_srcdir)/iris/iris-queue.h			\
	$(top_srcdir)/iris/iris-receiver.h		\
	$(top_srcdir)/iris/iris-rrobin.h		\
	$(top_srcdir)/iris/iris-scheduler.h		\
	$(top_srcdir)/iris/iris-scheduler-manager.h	\
	$(top_srcdir)/iris/iris-stack.h			\
	$(top_srcdir)/iris/iris-task.h			\
	$(top_srcdir)/iris/iris-thread.h		\
	$(top_srcdir)/iris/iris-types.h			\
	$(top_srcdir)/iris/iris-wsqueue.h		\
	$(top_srcdir)/iris/iris-wsscheduler.h		\
	$(NULL)

sources_private_h =						\
	$(top_srcdir)/iris/iris-arbiter-private.h		\
	$(top_srcdir)/iris/iris-receiver-private.h		\
	$(top_srcdir)/iris/iris-scheduler-private.h		\
	$(top_srcdir)/iris/iris-scheduler-manager-private.h	\
	$(top_srcdir)/iris/iris-task-private.h			\
	$(top_srcdir)/iris/iris-util.h				\
	$(top_srcdir)/iris/gstamppointer.h			\
	$(NULL)

sources_c = 						\
	iris-all-task.c					\
	iris-any-task.c					\
	iris-arbiter.c					\
	iris-free-list.c				\
	iris-lfqueue.c					\
	iris-lfscheduler.c				\
	iris-message.c					\
	iris-port.c					\
	iris-queue.c					\
	iris-receiver.c					\
	iris-rrobin.c					\
	iris-scheduler.c				\
	iris-scheduler-manager.c			\
	iris-stack.c					\
	iris-task.c					\
	iris-thread.c					\
	iris-util.c					\
	iris-wsqueue.c					\
	iris-wsscheduler.c				\
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

if PLATFORM_LINUX
libiris_@IRIS_API_VERSION@_la_CFLAGS += "-DLINUX"
endif

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


if ENABLE_INTROSPECTION

Iris-1.0.gir: $(sources_c)
Iris-1.0.gir: $(sources_public_h)
Iris-1.0.gir: $(G_IR_SCANNER)
	$(G_IR_SCANNER) -v --namespace Iris             \
		--nsversion=1.0                         \
		--add-include-path=$(srcdir)            \
		--add-include-path=.                    \
		--include=Gio-2.0                       \
		--library=iris-1.0                      \
		--output $@                             \
		--pkg gobject-2.0                       \
		--pkg gio-2.0                           \
		-I$(top_srcdir)                         \
		$(sources_c)                            \
		$(sources_public_h)

girdir = $(GIRDIR)
gir_DATA = Iris-1.0.gir

EXTRA_DIST += Iris-1.0.gir

typelibsdir = $(TYPELIBDIR)
typelibs_DATA = Iris-1.0.typelib

Iris-1.0.typelib: Iris-1.0.gir
	$(G_IR_COMPILER)                                \
	--includedir=$(srcdir)                          \
	--includedir=.                                  \
	--shared-library=iris-1.0                       \
	$(G_IR_COMPILER_OPTS)                           \
	$< -o $(top_builddir)/iris/$(@F)

endif # ENABLE_INTROSPECTION
