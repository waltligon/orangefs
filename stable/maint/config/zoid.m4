#
# Configure rules for ZOID
#
# See COPYING in top-level directory.
#
AC_DEFUN([AX_ZOID],
[
    dnl Configure options for ZOID install path.
    dnl --with-zoid=<dir>
    AC_ARG_WITH(zoid,
[  --with-zoid=<dir>         Location of the ZOID tree (default no ZOID)],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-zoid requires the path to your ZOID source tree.])
	elif test "$withval" != no ; then
	    ZOID_SRCDIR="$withval"
	fi
    )
    if test -n "$ZOID_SRCDIR" ; then
	save_cppflags="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS -Isrc/io/bmi -I$ZOID_SRCDIR/include -I$ZOID_SRCDIR/zbmi -I$ZOID_SRCDIR/zbmi/implementation"
	AC_CHECK_HEADER(zbmi.h,, AC_MSG_ERROR([Header zbmi.h not found.]))
	AC_CHECK_HEADER(zoid_api.h,, AC_MSG_ERROR([Header zoid_api.h not found.]))
	AC_CHECK_HEADER(zbmi_protocol.h,, AC_MSG_ERROR([Header zbmi_protocol.h not found.]))
	CPPFLAGS="$save_cppflags"
	BUILD_ZOID=1
    fi
    AC_SUBST(BUILD_ZOID)
    AC_SUBST(ZOID_SRCDIR)
])

dnl vim: set ft=config :
