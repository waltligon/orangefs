#
# Configure rules for MX
#
# Copyright (C) 2008 Pete Wyckoff <pw@osc.edu>
#
# See COPYING in top-level directory.
#
AC_DEFUN([AX_MX],
[
    dnl Configure options for MX install path.
    dnl --with-mx=<dir> is shorthand for
    dnl    --with-mx-includes=<dir>/include
    dnl    --with-mx-libs=<dir>/lib  (or lib64 if that exists)
    mx_home=
    AC_ARG_WITH(mx,
[  --with-mx=<dir>         Location of the MX install (default no MX)],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-mx requires the path to your MX tree.])
	elif test "$withval" != no ; then
	    mx_home="$withval"
	fi
    )
    AC_ARG_WITH(mx-includes,
[  --with-mx-includes=<dir>
                          Location of the MX includes],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-mx-includes requires path to MX headers.])
	elif test "$withval" != no ; then
	    MX_INCDIR="$withval"
	fi
    )
    AC_ARG_WITH(mx-libs,
[  --with-mx-libs=<dir>    Location of the MX libraries],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-mx-libs requires path to MX libraries.])
	elif test "$withval" != no ; then
	    MX_LIBDIR="$withval"
	fi
    )
    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-mx dir.
    if test -n "$mx_home" ; then
	if test -z "$MX_INCDIR"; then
	    MX_INCDIR=$mx_home/include
	fi
	if test -z "$MX_LIBDIR"; then
	    MX_LIBDIR=$mx_home/lib64
	    if test ! -d "$MX_LIBDIR" ; then
		MX_LIBDIR=$mx_home/lib
	    fi
	fi
    fi
    dnl If anything MX-ish was set, go look for header.
    if test -n "$MX_INCDIR$MX_LIBDIR" ; then
	save_cppflags="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS -I$MX_INCDIR -I$MX_INCDIR/mx"
	AC_CHECK_HEADER(myriexpress.h,,
			AC_MSG_ERROR([Header myriexpress.h not found.]))
	dnl Run test is not possible on a machine that does not have a MX NIC.
	dnl Link test would work, but just check for existence.
	if test ! -f $MX_LIBDIR/libmyriexpress.so ; then
	    if test ! -f $MX_LIBDIR/libmyriexpress.a ; then
		AC_MSG_ERROR([Neither MX library libmyriexpress.so or libmyriexpress.a found.])
	    fi
	fi
	BUILD_MX=1
	CPPFLAGS="$save_cppflags"
    fi
    AC_SUBST(BUILD_MX)
    AC_SUBST(MX_INCDIR)
    AC_SUBST(MX_LIBDIR)

    if test -n "$BUILD_MX" ; then
        dnl Check for existence of mx_decompose_endpoint_addr2
        save_ldflags="$LDFLAGS"
        LDFLAGS="-L$MX_LIBDIR $LDFLAGS"
	save_libs="$LIBS"
	LIBS="-lmyriexpress -lpthread $LIBS"
        save_cppflags="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$MX_INCDIR"

        AC_MSG_CHECKING(for mx_decompose_endpoint_addr2)
        AC_TRY_LINK([
            #include "mx_extensions.h"
            #include <stdlib.h>
        ], [ 
            mx_endpoint_addr_t epa;
            mx_decompose_endpoint_addr2(epa, NULL, NULL, NULL);
        ],
            AC_MSG_RESULT(yes),
            AC_MSG_RESULT(no)
	    AC_MSG_ERROR([Function mx_decompose_endpoint_addr2() not found.])
        )

        LDFLAGS="$save_ldflags"
        CPPFLAGS="$save_cppflags"
        LIBS="$save_libs"
    fi
])

dnl vim: set ft=config :
