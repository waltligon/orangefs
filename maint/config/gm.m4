#
# Configure rules for GM
#
# Copyright (C) 2008 Pete Wyckoff <pw@osc.edu>
#
# See COPYING in top-level directory.
#
AC_DEFUN([AX_GM],
[
    dnl Configure options for GM install path.
    dnl --with-gm=<dir> is shorthand for
    dnl    --with-gm-includes=<dir>/include
    dnl    --with-gm-libs=<dir>/lib  (or lib64 if that exists)
    gm_home=
    AC_ARG_WITH(gm,
[  --with-gm=<dir>         Location of the GM install (default no GM)],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-gm requires the path to your GM tree.])
	elif test "$withval" != no ; then
	    gm_home="$withval"
	fi
    )
    AC_ARG_WITH(gm-includes,
[  --with-gm-includes=<dir>
                          Location of the GM includes],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-gm-includes requires path to GM headers.])
	elif test "$withval" != no ; then
	    GM_INCDIR="$withval"
	fi
    )
    AC_ARG_WITH(gm-libs,
[  --with-gm-libs=<dir>    Location of the GM libraries],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-gm-libs requires path to GM libraries.])
	elif test "$withval" != no ; then
	    GM_LIBDIR="$withval"
	fi
    )
    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-gm dir.
    if test -n "$gm_home" ; then
	if test -z "$GM_INCDIR"; then
	    GM_INCDIR=$gm_home/include
	fi
	if test -z "$GM_LIBDIR"; then
	    GM_LIBDIR=$gm_home/lib64
	    if test ! -d "$GM_LIBDIR" ; then
		GM_LIBDIR=$gm_home/lib
	    fi
	fi
    fi
    dnl If anything GM-ish was set, go look for header.
    if test -n "$GM_INCDIR$GM_LIBDIR" ; then
	save_cppflags="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS -I$GM_INCDIR -I$GM_INCDIR/gm"
	AC_CHECK_HEADER(gm.h,, AC_MSG_ERROR([Header gm.h not found.]))
	dnl Run test is not possible on a machine that does not have a GM NIC.
	dnl Link test would work, but just check for existence.
	if test ! -f $GM_LIBDIR/libgm.so ; then
	    if test ! -f $GM_LIBDIR/libgm.a ; then
		AC_MSG_ERROR([Neither GM library libgm.so or libgm.a found.])
	    fi
	fi
	BUILD_GM=1
	CPPFLAGS="$save_cppflags"
    fi
    AC_SUBST(BUILD_GM)
    AC_SUBST(GM_INCDIR)
    AC_SUBST(GM_LIBDIR)
])

dnl vim: set ft=config :
