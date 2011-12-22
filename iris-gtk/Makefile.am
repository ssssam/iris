sources_public_h =										\
	$(top_srcdir)/iris-gtk/iris-gtk.h					\
	$(top_srcdir)/iris-gtk/gtk-iris-progress-dialog.h		\
	$(top_srcdir)/iris-gtk/gtk-iris-progress-info-bar.h		\
	$(NULL)

sources_private_h =											\
	$(top_srcdir)/iris-gtk/gtk-iris-progress-dialog-private.h	\
	gtk-iris-progress-info-bar-private.h						\
	$(NULL)

sources_c = 					\
	gtk-iris-progress-dialog.c		\
	gtk-iris-progress-info-bar.c	\
	$(NULL)


irisgtkincludedir = $(includedir)/iris-$(IRIS_API_VERSION)/iris-gtk

irisgtkinclude_DATA = $(sources_public_h)
irisgtkinclude_DATA += $(NULL)

lib_LTLIBRARIES = libiris-gtk-@IRIS_API_VERSION@.la

libiris_gtk_@IRIS_API_VERSION@_la_SOURCES = 	\
	$(sources_public_h) 						\
	$(sources_private_h) 						\
	$(sources_c) 						\
	$(BUILT_SOURCES) 					\
	iris-gtk.h
libiris_gtk_@IRIS_API_VERSION@_la_SOURCES += $(NULL)


libiris_gtk_@IRIS_API_VERSION@_la_CFLAGS =				\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/iris				\
	-DPREFIX=\""$(prefix)"\"				\
	-DLIBDIR=\""$(libdir)"\"				\
	-DG_LOG_DOMAIN=\"Iris\"					\
	-DG_DISABLE_DEPRECATED					\
	-DIRIS_COMPILATION					\
	$(IRIS_MAINTAINER_CFLAGS)				\
	$(IRIS_DEBUG_CFLAGS)					\
	$(IRIS_GTK_CFLAGS)						\
	$(NULL)

if PLATFORM_LINUX
libiris_gtk_@IRIS_API_VERSION@_la_CFLAGS += "-DLINUX"
endif
if PLATFORM_DARWIN
libiris_gtk_@IRIS_API_VERSION@_la_CFLAGS += "-DDARWIN"
endif
if PLATFORM_WIN32
libiris_gtk_@IRIS_API_VERSION@_la_CFLAGS += "-DWIN32"
endif

if ENABLE_PROFILING
libiris_gtk_@IRIS_API_VERSION@_la_CFLAGS += "-DENABLE_PROFILING"
endif

libiris_gtk_@IRIS_API_VERSION@_la_LDFLAGS = \
	$(IRIS_GLIB_LT_LDFLAGS)                 \
	-export-dynamic                         \
	-rpath $(libdir)                        \
	$(NULL)


libiris_gtk_@IRIS_API_VERSION@_la_LIBADD =  \
	$(IRIS_LIBS)                            \
	$(IRIS_GTK_LIBS)                        \
	$(top_builddir)/iris/libiris-1.0.la

EXTRA_DIST =							\
	iris-gtk.h						\
	$(NULL)


if ENABLE_INTROSPECTION

IrisGtk-1.0.gir: $(sources_c)
IrisGtk-1.0.gir: $(sources_public_h)
IrisGtk-1.0.gir: $(G_IR_SCANNER)
	$(G_IR_SCANNER) -v --namespace IrisGtk		\
		--nsversion=1.0				\
		--add-include-path=$(srcdir)		\
		--add-include-path=.			\
		--add-include-path=../iris/		\
		--include=Gio-2.0			\
		--include=Iris-1.0			\
		--library=iris-gtk-1.0			\
		--output $@				\
		--pkg gobject-2.0			\
		--pkg gio-2.0				\
		--pkg gtk-3.0				\
		-I$(top_srcdir)				\
		$(sources_c)				\
		$(sources_public_h)

girdir = $(GIRDIR)
gir_DATA = IrisGtk-1.0.gir

EXTRA_DIST += IrisGtk-1.0.gir

typelibsdir = $(TYPELIBDIR)
typelibs_DATA = IrisGtk-1.0.typelib

IrisGtk-1.0.typelib: IrisGtk-1.0.gir
	$(G_IR_COMPILER)				\
	--includedir=$(srcdir)				\
	--includedir=.					\
	--shared-library=iris-gtk-1.0			\
	$(G_IR_COMPILER_OPTS)				\
	$< -o $(top_builddir)/iris/$(@F)

endif # ENABLE_INTROSPECTION
