m4_define([iris_major_version], [0])
m4_define([iris_minor_version], [1])
m4_define([iris_micro_version], [1])
m4_define([iris_version], [iris_major_version.iris_minor_version.iris_micro_version])
m4_define([iris_interface_age], [0])
m4_define([iris_binary_age], [m4_eval(100 * iris_minor_version + iris_micro_version)])
m4_define([lt_current], [m4_eval(100 * iris_minor_version + iris_micro_version - iris_interface_age)])
m4_define([lt_revision], [iris_interface_age])
m4_define([lt_age], [m4_eval(iris_binary_age - iris_interface_age)])

m4_define([glib_req_version], [2.16])
m4_define([gtk_req_version], [3.0.0])

AC_PREREQ([2.59])

AC_INIT([iris],
        [iris_version],
        [],
        [iris])

AC_CONFIG_SRCDIR([iris/iris.h])
AC_CONFIG_MACRO_DIR([m4])

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

IRIS_EXTRA_PACKAGES=
IRIS_EXTRA_LIBS=
IRIS_EXTRA_CFLAGS=

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

PKG_CHECK_MODULES(IRIS, gobject-2.0 >= glib_req_version gthread-2.0 >= glib_req_version gio-2.0 >= glib_req_version)

AM_CONDITIONAL(ENABLE_GLIB_TEST, test "x$enable_glibtest" = "xyes")

dnl = Host specific features ===============================================
AC_MSG_CHECKING([host platform characteristics])
case "$host" in
	*-*-mingw*|*-*-cygwin*)
		platform_darwin=no
		platform_win32=yes
		;;
	*-*-darwin*)
		platform_darwin=yes
		platform_win32=no
		;;
esac
AC_MSG_RESULT(ok)
AM_CONDITIONAL(PLATFORM_WIN32, test x$platform_win32 = xyes)
AM_CONDITIONAL(PLATFORM_DARWIN, test x$platform_darwin = xyes)

dnl = Build iris-gtk ========================================================

AC_ARG_ENABLE([gtk],
              AS_HELP_STRING([--enable-gtk],[Enable iris-gtk widget library]),
              [enablegtk=$enableval],
              [enable_gtk=yes])
    
if test "x$enable_gtk" = "xyes"; then
	PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $gtk_req_version)
fi

AC_DEFINE([ENABLE_GTK],[1],[Define to compile with GTK+ support])
AM_CONDITIONAL(ENABLE_GTK, [test "x$enable_gtk" = "xyes"])

IRIS_GTK_CFLAGS="$IRIS_CFLAGS $GTK_CFLAGS"
IRIS_GTK_LIBS="$IRIS_LIBS $GTK_LIBS"

AC_SUBST(IRIS_GTK_CFLAGS)
AC_SUBST(IRIS_GTK_LIBS)


dnl = Enable Vala ==========================================================
enable_vala=no
PKG_CHECK_MODULES(VALA, vala-1.0, enable_vala=yes, enable_vala=no)
if test "x$enable_vala" = "xyes"; then
	VAPI_DIR=`pkg-config --variable=vapidir vala-1.0`
else
	VAPI_DIR=
fi
AM_CONDITIONAL(ENABLE_VALA, [test "x$enable_vala" = "xyes"])
AC_SUBST(VAPI_DIR)

dnl = Enable profiling =====================================================
AC_ARG_ENABLE([profiling],
              AC_HELP_STRING([--enable-profiling],
              		     [turn on timer profiling]),
              ,
              enable_profiling=no)
AM_CONDITIONAL(ENABLE_PROFILING, [test "x$enable_profiling" = "xyes"])

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
  IRIS_MAINTAINER_CFLAGS="-Werror -Wall -Wshadow -Wcast-align -Wno-uninitialized -Wformat-security -Winit-self"
fi

AC_SUBST(IRIS_MAINTAINER_CFLAGS)

dnl = Check for GObject Introspection ======================================

AC_MSG_CHECKING([whether GObject introspection is requested])
AC_ARG_ENABLE([introspection],
	      AS_HELP_STRING([--enable-introspection],[Enable GObject introspection]),
	      [],[enable_introspection=no])
AC_MSG_RESULT([$enable_introspection])

G_IR_SCANNER=
G_IR_COMPILER=
G_IR_GENERATE=
GIRDIR=
TYPELIBDIR=
if test "$enable_introspection" = "yes"; then
   G_IR_SCANNER=`pkg-config --variable=g_ir_scanner gobject-introspection-1.0`
   G_IR_COMPILER=`pkg-config --variable=g_ir_compiler gobject-introspection-1.0`
   G_IR_GENERATE=`pkg-config --variable=g_ir_generate gobject-introspection-1.0`
   GIRDIR=`pkg-config --variable=girdir gobject-introspection-1.0`
   TYPELIBDIR="$(pkg-config --variable=typelibdir gobject-introspection-1.0)"
fi

AC_SUBST(G_IR_SCANNER)
AC_SUBST(G_IR_COMPILER)
AC_SUBST(G_IR_GENERATE)
AC_SUBST(GIRDIR)
AC_SUBST(TYPELIBDIR)

AM_CONDITIONAL([ENABLE_INTROSPECTION],[test "$enable_introspection" = "yes"])

dnl = Enable Python support ================================================
AC_MSG_CHECKING([whether Python support is requested])
AC_ARG_ENABLE([python],
	AS_HELP_STRING([--enable-python],[Enable python support]),
	[enable_python=$enableval enable_python=$enableval],
	[enable_python=autodetect enable_python=no])
AC_MSG_RESULT([$enable_python])
if test "x$enable_python" != "xno"; then
	AM_PATH_PYTHON([2.3],[],[no])
	if test "x$PYTHON" = "x:"; then
		enable_python=no
	fi
fi
if test "x$enable_python" != "xno"; then
	AM_CHECK_PYTHON_HEADERS([],[enable_python=no])
fi
if test "x$enable_python" != "xno"; then
	PY_PREFIX=`$PYTHON -c 'import sys ; print sys.prefix'`
	PY_EXEC_PREFIX=`$PYTHON -c 'import sys ; print sys.exec_prefix'`
	PYTHON_LIBS="-lpython$PYTHON_VERSION"
	PYTHON_LIB_LOC="-L$PY_EXEC_PREFIX/lib/python$PYTHON_VERSION/config"
	PYTHON_CFLAGS="-I$PY_PREFIX/include/python$PYTHON_VERSION"
	PYTHON_MAKEFILE="$PY_EXEC_PREFIX/lib/python$PYTHON_VERSION/config/Makefile"
	PYTHON_LOCALMODLIBS=`sed -n -e 's/^LOCALMODLIBS=\(.*\)/\1/p' $PYTHON_MAKEFILE`
	PYTHON_BASEMODLIBS=`sed -n -e 's/^BASEMODLIBS=\(.*\)/\1/p' $PYTHON_MAKEFILE`
	PYTHON_OTHER_LIBS=`sed -n -e 's/^LIBS=\(.*\)/\1/p' $PYTHON_MAKEFILE`
	PYTHON_EXTRA_LIBS="$PYTHON_LOCALMODLIBS $PYTHON_BASEMODLIBS $PYTHON_OTHER_LIBS"
	AC_SUBST([PYTHON_LIBS])
	AC_SUBST([PYTHON_LIB_LOC])
	AC_SUBST([PYTHON_CFLAGS])
	AC_SUBST([PYTHON_EXTRA_LIBS])
fi
if test "x$enable_python" != "xyes"; then
	if test "x$enable_python" = "xyes"; then
		AC_MSG_ERROR([Python not found])
	elif test "x$enable_python" = "xautodetect"; then
		enable_python=no
		AC_MSG_WARN([Python not found, disabling python support])
	fi
fi
if test "x$enable_python" != "xno"; then
	PYGOBJECT_REQUIRED=2.15.3
	PYGTK_REQUIRED=2.12.0

	PKG_CHECK_MODULES([PYGTK], [
		pygobject-2.0 >= $PYGOBJECT_REQUIRED
		pygtk-2.0 >= $PYGTK_REQUIRED],
		[],
		[
		enable_python=no
		if test "x$enable_python" = "xyes"; then
			AC_MSG_ERROR([$PYGTK_PKG_ERRORS])
		elif test "x$enable_python" = "xautodetect"; then
                        enable_python=no
			AC_MSG_WARN([$PYGTK_PKG_ERRORS])
			AC_MSG_WARN([Disabling python support])
		fi
		])

	AC_SUBST([PYGTK_CFLAGS])
	AC_SUBST([PYGTK_LIBS])
fi
if test "x$enable_python" != "xno"; then
	AC_MSG_CHECKING([for pygtk defs])
	PYGTK_DEFSDIR=`$PKG_CONFIG --variable=defsdir pygtk-2.0`
	AC_MSG_RESULT([$PYGTK_DEFSDIR])

	AC_MSG_CHECKING([for pygtk codegen])
	PYGTK_CODEGEN="$PYTHON `$PKG_CONFIG --variable=codegendir pygtk-2.0`/codegen.py"
	AC_MSG_RESULT([$PYGTK_CODEGEN])

	AC_MSG_CHECKING([for pygtk h2def])
	PYGTK_H2DEF="$PYTHON `$PKG_CONFIG --variable=codegendir pygtk-2.0`/h2def.py"
	AC_MSG_RESULT([$PYGTK_H2DEF])

	AC_SUBST([PYGTK_DEFSDIR])
	AC_SUBST([PYGTK_CODEGEN])
	AC_SUBST([PYGTK_H2DEF])

	dnl Check for -fno-strict-aliasing
	FLAGS="-fno-strict-aliasing"
	save_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS $FLAGS"
	AC_MSG_CHECKING([whether [$]CC understands $FLAGS])
	AC_TRY_COMPILE([], [], [compiler_has_option=yes], [compiler_has_option=no])
	CFLAGS="$save_CFLAGS"
	AC_MSG_RESULT($compiler_has_option)
	if test $compiler_has_option = yes; then
		NO_STRICT_ALIASING_CFLAGS="$FLAGS"
	fi
	AC_SUBST([NO_STRICT_ALIASING_CFLAGS])
fi
if test "x$enable_python" != "xno" -a "x$enable_python" != "xno"; then
	enable_python=yes
	AC_DEFINE([ENABLE_PYTHON],[1],[Define to compile with python support])
fi
AM_CONDITIONAL([ENABLE_PYTHON],[test "x$enable_python" = "xyes"])


dnl = Misc =================================================================
# for automake
AC_SUBST(IRIS_CFLAGS)
AC_SUBST(IRIS_LIBS)

# for pkg-config
AC_SUBST(IRIS_EXTRA_PACKAGES)
AC_SUBST(IRIS_EXTRA_LIBS)
AC_SUBST(IRIS_EXTRA_CFLAGS)

GTK_DOC_CHECK([1.11])
SHAVE_INIT([autotools], [enable])

AC_CONFIG_FILES([
        autotools/shave-libtool
        autotools/shave
        Makefile
        iris/Makefile
        iris/iris-version.h
        iris-gtk/Makefile
        bindings/Makefile
        bindings/vala/Makefile
        bindings/python/Makefile
        examples/Makefile
        tests/Makefile
        doc/Makefile
        doc/reference/Makefile
        doc/reference/version.xml
        iris-1.0.pc
        iris-gtk-1.0.pc
])

AC_OUTPUT

echo ""
echo " Iris $VERSION"
echo ""
echo "   Prefix...................:  ${prefix}"
echo "   Debug level..............:  ${enable_debug}"
echo "   Maintainer Compiler flags:  ${enable_maintainer_flags}"
echo "   Profiling................:  ${enable_profiling}"
echo "   Build API reference......:  ${enable_gtk_doc}"
echo "   Enable test suite........:  ${enable_glibtest}"
echo "   Enable GTK+ widgets......:  ${enable_gtk}"
echo ""
echo " Preview Bindings"
echo ""
echo "   GObject Introspection....:  ${enable_introspection}"
echo "   Vala Bindings............:  ${enable_vala}"
echo "   Python Bindings..........:  ${enable_python}"
echo ""

