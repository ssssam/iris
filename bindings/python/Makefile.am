INCLUDES = $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)

defsdir = $(datadir)/pygtk/2.0/defs
defs_DATA = iris.defs

pkgpythondir = $(pyexecdir)/gtk-2.0
pkgpyexecdir = $(pyexecdir)/gtk-2.0

pkgpyexec_LTLIBRARIES = iris.la
iris_la_CFLAGS = $(GTASK_CFLAGS) $(GTHREAD_CFLAGS) -I$(top_srcdir)
iris_la_LDFLAGS = -module -avoid-version -export-symbols-regex initiris
iris_la_LIBADD = $(GTASK_LIBS) $(GLIB_LIBS) $(GTHREAD_LIBS) $(top_builddir)/iris/libiris-1.0.la
iris_la_SOURCES = irismodule.c pyirismarshal.c

nodist_iris_la_SOURCES = iris.c

CLEANFILES = iris.c
EXTRA_DIST = iris.defs iris.override pyirismarshal.list
iris.c: iris.defs iris.override

.defs.c:
	(cd $(srcdir) \
	 && $(PYGTK_CODEGEN) \
	    --override $*.override \
	    --prefix py$* $*.defs) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c

BUILT_SOURCES = pyirismarshal.h pyirismarshal.c

STAMP_FILES = stamp-pyirismarshal.h

pyirismarshal.h: stamp-pyirismarshal.h
	@true
stamp-pyirismarshal.h: Makefile $(srcdir)/pyirismarshal.list
	$(GLIB_GENMARSHAL) \
		--prefix=_pyiris_marshal \
		--header \
	$(srcdir)/pyirismarshal.list > xgen-jmh && \
	(cmp -s xgen-jmh pyirismarshal.h || cp -f xgen-jmh pyirismarshal.h) && \
	rm -f xgen-jmh && \
	echo timestamp > $(@F)

pyirismarshal.c: Makefile $(srcdir)/pyirismarshal.list
	(echo "#include \"pyirismarshal.h\"" ; \
	 $(GLIB_GENMARSHAL) \
		--prefix=_pyiris_marshal \
		--body \
	 $(srcdir)/pyirismarshal.list ) > xgen-jmc && \
	cp -f xgen-jmc pyirismarshal.c && \
	rm -f xgen-jmc

