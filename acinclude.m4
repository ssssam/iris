dnl Make automake/libtool output more friendly to humans
dnl
dnl SHAVE_INIT([shavedir],[default_mode])
dnl
dnl shavedir: the directory where the shave scripts are, it defaults to
dnl           $(top_builddir)
dnl default_mode: (enable|disable) default shave mode.  This parameter
dnl               controls shave's behaviour when no option has been
dnl               given to configure.  It defaults to disable.
dnl
dnl * SHAVE_INIT should be called late in your configure.(ac|in) file (just
dnl   before AC_CONFIG_FILE/AC_OUTPUT is perfect.  This macro rewrites CC and
dnl   LIBTOOL, you don't want the configure tests to have these variables
dnl   re-defined.
dnl * This macro requires GNU make's -s option.

AC_DEFUN([_SHAVE_ARG_ENABLE],
[
  AC_ARG_ENABLE([shave],
    AS_HELP_STRING(
      [--enable-shave],
      [use shave to make the build pretty [[default=$1]]]),,
      [enable_shave=$1]
    )
])

AC_DEFUN([SHAVE_INIT],
[
  dnl you can tweak the default value of enable_shave
  m4_if([$2], [enable], [_SHAVE_ARG_ENABLE(yes)], [_SHAVE_ARG_ENABLE(no)])

  if test x"$enable_shave" = xyes; then
    dnl where can we find the shave scripts?
    m4_if([$1],,
      [shavedir="$ac_pwd"],
      [shavedir="$ac_pwd/$1"])
    AC_SUBST(shavedir)

    dnl make is now quiet
    AC_SUBST([MAKEFLAGS], [-s])
    AC_SUBST([AM_MAKEFLAGS], ['`test -z $V && echo -s`'])

    dnl we need sed
    AC_CHECK_PROG(SED,sed,sed,false)

    dnl substitute libtool
    SHAVE_SAVED_LIBTOOL=$LIBTOOL
    LIBTOOL="${SHELL} ${shavedir}/shave-libtool '${SHAVE_SAVED_LIBTOOL}'"
    AC_SUBST(LIBTOOL)

    dnl substitute cc/cxx
    SHAVE_SAVED_CC=$CC
    SHAVE_SAVED_CXX=$CXX
    SHAVE_SAVED_FC=$FC
    SHAVE_SAVED_F77=$F77
    CC="${SHELL} ${shavedir}/shave cc ${SHAVE_SAVED_CC}"
    CXX="${SHELL} ${shavedir}/shave cxx ${SHAVE_SAVED_CXX}"
    FC="${SHELL} ${shavedir}/shave fc ${SHAVE_SAVED_FC}"
    F77="${SHELL} ${shavedir}/shave f77 ${SHAVE_SAVED_F77}"
    AC_SUBST(CC)
    AC_SUBST(CXX)
    AC_SUBST(FC)
    AC_SUBST(F77)

    V=@
  else
    V=1
  fi
  Q='$(V:1=)'
  AC_SUBST(V)
  AC_SUBST(Q)
])

## this one is commonly used with AM_PATH_PYTHONDIR ...
dnl AM_CHECK_PYMOD(MODNAME [,SYMBOL [,ACTION-IF-FOUND [,ACTION-IF-NOT-FOUND]]])
dnl Check if a module containing a given symbol is visible to python.
AC_DEFUN(AM_CHECK_PYMOD,
[AC_REQUIRE([AM_PATH_PYTHON])
py_mod_var=`echo $1['_']$2 | sed 'y%./+-%__p_%'`
AC_MSG_CHECKING(for ifelse([$2],[],,[$2 in ])python module $1)
AC_CACHE_VAL(py_cv_mod_$py_mod_var, [
ifelse([$2],[], [prog="
import sys
try:
        import $1
except ImportError:
        sys.exit(1)
except:
        sys.exit(0)
sys.exit(0)"], [prog="
import $1
$1.$2"])
if $PYTHON -c "$prog" 1>&AC_FD_CC 2>&AC_FD_CC
  then
    eval "py_cv_mod_$py_mod_var=yes"
  else
    eval "py_cv_mod_$py_mod_var=no"
  fi
])
py_val=`eval "echo \`echo '$py_cv_mod_'$py_mod_var\`"`
if test "x$py_val" != xno; then
  AC_MSG_RESULT(yes)
  ifelse([$3], [],, [$3
])dnl
else
  AC_MSG_RESULT(no)
  ifelse([$4], [],, [$4
])dnl
fi
])

dnl a macro to check for ability to create python extensions
dnl  AM_CHECK_PYTHON_HEADERS([ACTION-IF-POSSIBLE], [ACTION-IF-NOT-POSSIBLE])
dnl function also defines PYTHON_INCLUDES
AC_DEFUN([AM_CHECK_PYTHON_HEADERS],
[AC_REQUIRE([AM_PATH_PYTHON])
AC_MSG_CHECKING(for headers required to compile python extensions)
dnl deduce PYTHON_INCLUDES
py_prefix=`$PYTHON -c "import sys; print sys.prefix"`
py_exec_prefix=`$PYTHON -c "import sys; print sys.exec_prefix"`
PYTHON_INCLUDES="-I${py_prefix}/include/python${PYTHON_VERSION}"
if test "$py_prefix" != "$py_exec_prefix"; then
  PYTHON_INCLUDES="$PYTHON_INCLUDES -I${py_exec_prefix}/include/python${PYTHON_VERSION}"
fi
AC_SUBST(PYTHON_INCLUDES)
dnl check if the headers exist:
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $PYTHON_INCLUDES"
AC_TRY_CPP([#include <Python.h>],dnl
[AC_MSG_RESULT(found)
$1],dnl
[AC_MSG_RESULT(not found)
$2])
CPPFLAGS="$save_CPPFLAGS"
])
