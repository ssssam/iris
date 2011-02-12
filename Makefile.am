SUBDIRS = iris 

if ENABLE_GTK
SUBDIRS += iris-gtk
endif

SUBDIRS += bindings doc examples

if ENABLE_GLIB_TEST
SUBDIRS += tests
endif

DIST_SUBDIRS = iris iris-gtk bindings doc tests examples

pcfiles = iris-1.0.pc \
          iris-gtk-1.0.pc

pkgconfig_DATA = $(pcfiles)
pkgconfigdir = $(libdir)/pkgconfig

EXTRA_DIST = iris-1.0.pc.in \
             iris-gtk-1.0.pc.in

CLEANFILES = $(pcfiles) test-report.xml

DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc --enable-maintainer-flags

dist-hook:
	@if test -d "$(srcdir)/.git"; then \
	  (cd "$(srcdir)" && \
	   $(top_srcdir)/missing --run git log --stat ) > ChangeLog.tmp \
	   && mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
	   || (rm -f ChangeLog.tmp; \
	       echo Failed to generate ChangeLog >&2); \
	else \
	  echo A git checkout is required to generate a ChangeLog >&2; \
	fi
