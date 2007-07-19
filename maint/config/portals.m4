AC_DEFUN([AX_PORTALS],
[
    dnl
    dnl Configure to build Portals BMI method, if requested and available.
    dnl --with-portals=<dir> is shorthand for
    dnl    --with-portals-includes=<dir>/include
    dnl    --with-portals-libs=<dir>/lib  (or lib64 if that exists)
    portals_home=
    AC_ARG_WITH(portals,
    [  --with-portals=<dir>     Location of the Portals install (default no Portals)],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-portals requires the path to your Portals tree.])
	elif test "$withval" != no ; then
	    portals_home="$withval"
	fi
    )
    AC_ARG_WITH(portals-includes,
    [  --with-portals-includes=<dir>  Location of the Portals includes],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-portals-includes requires path to Portals headers.])
	elif test "$withval" != no ; then
	    PORTALS_INCDIR="$withval"
	fi
    )
    AC_ARG_WITH(portals-libs,
    [  --with-portals-libs=<dir>      Location of the Portals libraries],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-portals-libs requires path to Portals libraries.])
	elif test "$withval" != no ; then
	    PORTALS_LIBDIR="$withval"
	fi
    )
    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-portals dir.
    if test -n "$portals_home" ; then
	if test -z "$PORTALS_INCDIR"; then
	    PORTALS_INCDIR=$portals_home/include
	fi
	if test -z "$PORTALS_LIBDIR"; then
	    PORTALS_LIBDIR=$portals_home/lib64
	    if test ! -d "$PORTALS_LIBDIR" ; then
		PORTALS_LIBDIR=$portals_home/lib
	    fi
	fi
    fi

    dnl If Portals, go verify existence of header.
    if test -n "$PORTALS_INCDIR$PORTALS_LIBDIR" ; then
	save_cppflags="$CPPFLAGS"
	save_ldflags="$LDFLAGS"
	save_libs="$LIBS"
	CPPFLAGS="$CPPFLAGS -I$PORTALS_INCDIR"
	LDFLAGS="$LDFLAGS -L$PORTALS_LIBDIR"
	LIBS="$LIBS -lp3api -lp3lib -lp3utcp -lp3rt -lpthread"
	AC_CHECK_HEADER(portals3.h,,
	    AC_MSG_ERROR([Header portals3.h not found.]))
	dnl Run test is not always possible, esp when cross-compiling or on
	dnl a box that does not have the hardware.
	AC_TRY_LINK(
	    [#include <portals3.h>],
	    [
		int m, n;
		m = PtlInit(&n);
	    ],
	    [AC_MSG_RESULT(ok)],
	    AC_MSG_ERROR([Could not link Portals library.]))
	BUILD_PORTALS=1
	CPPFLAGS="$save_cppflags"
	LDFLAGS="$save_ldflags"
	LIBS="$save_libs"
    fi
    AC_SUBST(BUILD_PORTALS)
    AC_SUBST(PORTALS_INCDIR)
    AC_SUBST(PORTALS_LIBDIR)
])

dnl vim: set ft=config : 
