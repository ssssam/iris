m4_define([iris_major_version], [0])
m4_define([iris_minor_version], [1])
m4_define([iris_micro_version], [0])
m4_define([iris_version], [iris_major_version.iris_minor_version.iris_micro_version])
m4_define([iris_interface_age], [0])
m4_define([iris_binary_age], [m4_eval(100 * iris_minor_version + iris_micro_version)])
m4_define([lt_current], [m4_eval(100 * iris_minor_version + iris_micro_version - iris_interface_age)])
m4_define([lt_revision], [iris_interface_age])
m4_define([lt_age], [m4_eval(iris_binary_age - iris_interface_age)])

m4_define([glib_req_version], [2.16])

AC_PREREQ([2.59])

AC_INIT([iris],
        [iris_version],
        [],
        [iris])

AC_CONFIG_SRCDIR([iris/iris.h])

AM_INIT_AUTOMAKE([1.10])
AM_CONFIG_HEADER([config.h])

AM_MAINTAINER_MODE

AM_PROG_CC_C_O
AM_PATH_GLIB_2_0

IRIS_MAJOR_VERSION=iris_major_version
IRIS_MINOR_VERSION=iris_minor_version
IRIS_MICRO_VERSION=iris_micro_version
IRIS_API_VERSION=1.0
AC_SUBST(IRIS_MAJOR_VERSION)
AC_SUBST(IRIS_MICRO_VERSION)
AC_SUBST(IRIS_MINOR_VERSION)
AC_SUBST(IRIS_API_VERSION)
AC_SUBST(IRIS_VERSION)

IRIS_LT_CURRENT=lt_current
IRIS_LT_REVISION=lt_revision
IRIS_LT_AGE=lt_age
IRIS_LT_VERSION="$IRIS_LT_CURRENT:$IRIS_LT_REVISION:$IRIS_LT_AGE"
IRIS_LT_LDFLAGS="-version-info $IRIS_LT_VERSION"
AC_SUBST(IRIS_LT_LDFLAGS)

AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_ISC_POSIX
AC_HEADER_STDC
AC_CHECK_HEADERS([unistd.h])
AC_C_CONST
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_PATH_PROG([GLIB_GENMARSHAL], [glib-genmarshal])
AC_PATH_PROG([GLIB_MKENUMS], [glib-mkenums])
AC_PATH_PROG([GTESTER], [gtester])
AC_PATH_PROG([GTESTER_REPORT], [gtester-report])

AM_CONDITIONAL(PLATFORM_LINUX, echo x$host| grep -q linux)

PKG_CHECK_MODULES(IRIS, gobject-2.0 >= glib_req_version gthread-2.0 >= glib_req_version)
AC_SUBST(IRIS_CFLAGS)
AC_SUBST(IRIS_LIBS)

AM_CONDITIONAL(ENABLE_GLIB_TEST, test "x$enable_glibtest" = "xyes")

dnl = Enable debug level ===================================================

m4_define([debug_default],
          m4_if(m4_eval(iris_minor_version % 2), [1], [yes], [minimum]))

AC_ARG_ENABLE([debug],
              AC_HELP_STRING([--enable-debug=@<:@no/minimum/yes@:>@],
                             [turn on debugging @<:@default=debug_default@:>@]),
,
              enable_debug=debug_default)

if test "x$enable_debug" = "xyes"; then
  test "$cflags_set" = set || CFLAGS="$CFLAGS -g"
  IRIS_DEBUG_CFLAGS="-DIRIS_ENABLE_DEBUG"
else
  if test "x$enable_debug" = "xno"; then
    IRIS_DEBUG_CFLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS -DG_DISABLE_CAST_CHECKS"
  else # minimum
    IRIS_DEBUG_CFLAGS="-DIRIS_ENABLE_DEBUG -DG_DISABLE_CAST_CHECKS"
  fi
fi

AC_SUBST(IRIS_DEBUG_CFLAGS)

dnl = Enable strict compiler flags =========================================

# use strict compiler flags only on development releases
m4_define([maintainer_flags_default], m4_if(m4_eval(iris_minor_version % 2), [1], [yes], [no]))
AC_ARG_ENABLE([maintainer-flags],
              AC_HELP_STRING([--enable-maintainer-flags=@<:@no/yes@:>@],
                             [Use strict compiler flags @<:@default=maintainer_flags_default@:>@]),,
              enable_maintainer_flags=maintainer_flags_default)

if test "x$enable_maintainer_flags" = "xyes"; then
  IRIS_MAINTAINER_CFLAGS="-Werror -Wall -Wshadow -Wcast-align -Wno-uninitialized -Wempty-body -Wformat-security -Winit-self"
fi

AC_SUBST(IRIS_MAINTAINER_CFLAGS)

GTK_DOC_CHECK([1.11])

SHAVE_INIT([build/autotools], [enable])

AC_CONFIG_FILES([
        build/autotools/shave-libtool
        build/autotools/shave
        Makefile
        iris/Makefile
        iris/iris-version.h
        examples/Makefile
        tests/Makefile
        doc/Makefile
        doc/reference/Makefile
        doc/reference/version.xml
        iris.pc
])

AC_OUTPUT

echo ""
echo " Iris $VERSION"
echo ""
echo "                            Prefix: ${prefix}"
echo "                       Debug level: ${enable_debug}"
echo "         Maintainer Compiler flags: ${enable_maintainer_flags}"
echo "               Build API reference: ${enable_gtk_doc}"
echo "                 Enable test suite: ${enable_glibtest}"
echo ""
