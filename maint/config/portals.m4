AC_DEFUN([AX_PORTALS],
[
    dnl
    dnl Configure to build Portals BMI method, if requested and available.
    dnl Use
    dnl   --with-portals       To find include files and libraries in standard
    dnl                        system paths.
    dnl   --with-portals=<dir> To specify a location that has include and lib
    dnl                        (or lib64) subdirectories with the goods.
    dnl
    dnl Or specify the -I an -L and -l flags exactly using, e.g.:
    dnl
    dnl   --with-portals-includes="-I<dir>"
    dnl   --with-portals-libs="-L<dir> -l<name>"
    dnl
    dnl The C file uses #include <portals/portals3.h>, so choose your include
    dnl path accordingly.  If it did not do this, portals/errno.h would sit in
    dnl front of the system version.
    dnl
    use_portals=
    home=
    incs=
    libs=
    AC_ARG_WITH(portals,
    [  --with-portals[=<dir>]   Location of the Portals install (default no Portals)],
	if test -z "$withval" -o "$withval" = yes ; then
	    use_portals=yes
	elif test "$withval" != no ; then
	    home="$withval"
	fi
    )
    AC_ARG_WITH(portals-includes,
[  --with-portals-includes=<dir>
                          Extra CFLAGS to specify Portals includes],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-portals-includes requires an argument.])
	elif test "$withval" != no ; then
	    incs="$withval"
	fi
    )
    AC_ARG_WITH(portals-libs,
[  --with-portals-libs=<dir>
                          Extra LIBS to link Portals libraries],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-portals-libs requires an argument.])
	elif test "$withval" != no ; then
	    libs="$withval"
	fi
    )
    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-portals dir.
    if test -n "$home" ; then
	if test -z "$incs"; then
	    incs=-I$home/include
	fi
	if test -z "$libs"; then
	    libs=-L$home/lib64
	    if test ! -d "$home/lib64" ; then
		libs=-L$home/lib
	    fi
	fi
    fi

    dnl
    dnl Look for headers and libs.
    dnl
    BUILD_PORTALS=
    PORTALS_INCS=
    PORTALS_LIBS=
    if test "X$use_portals$home$incs$libs" != X ; then
	# Save stuff
	save_cppflags="$CPPFLAGS"
	save_libs="$LIBS"

	PORTALS_INCS="$incs"
	CPPFLAGS="$CPPFLAGS $PORTALS_INCS"

	PORTALS_LIBS="$libs"
	LIBS="$save_libs $PORTALS_LIBS"

	AC_MSG_CHECKING([for portals3.h header])
	ok=no
	AC_TRY_COMPILE(
	    [#include <portals/portals3.h>],
	    [int m, n; m = PtlInit(&n);],
	    [ok=yes])

	if test "$ok" = yes ; then
	    AC_MSG_RESULT([yes])
	else
	    AC_MSG_RESULT([no])
	    AC_MSG_ERROR([Header portals/portals3.h not found.])
	fi

	dnl try without first, for Cray, then try TCP version
	dnl Run test is not always possible, esp when cross-compiling or on
	dnl a box that does not have the hardware.
	AC_MSG_CHECKING([for portals libraries])
	ok=no
	AC_TRY_LINK(
	    [#include <portals/portals3.h>],
	    [int m, n; m = PtlInit(&n);],
	    [ok=yes])

	if test "$ok" = no ; then
	    PORTALS_LIBS="$libs -lportals"
	    LIBS="$save_libs $PORTALS_LIBS"
	    AC_TRY_LINK(
		[#include <portals/portals3.h>],
		[int m, n; m = PtlInit(&n);],
		[ok=yes])
	fi

	if test "$ok" = no ; then
	    PORTALS_LIBS="$libs -lp3api -lp3lib -lp3utcp -lp3rt -lpthread"
	    LIBS="$save_libs $PORTALS_LIBS"
	    AC_TRY_LINK(
		[#include <portals/portals3.h>],
		[int m, n; m = PtlInit(&n);],
		[ok=yes])
	fi

	if test "$ok" = yes ; then
	    AC_MSG_RESULT([yes])
	    BUILD_PORTALS=1
	else
	    AC_MSG_RESULT([no])
	    AC_MSG_ERROR([Could not link Portals library.])
	fi

	#
	# Check for API variations.
	#
	AC_CHECK_FUNCS(PtlErrorStr)
	AC_CHECK_FUNCS(PtlEventKindStr)

	AC_TRY_COMPILE(
	    [#include <portals/portals3.h>],
	    [int m; ptl_process_id_t any_pid;
	     m = PtlACEntry(0, 0, any_pid, (ptl_uid_t) -1, (ptl_jid_t) -1, 0);],
	    AC_DEFINE(HAVE_PTLACENTRY_JID, 1,
		      [Define if have PtlACEntry with jid argument.]))

	# Reset
	CPPFLAGS="$save_cppflags"
	LIBS="$save_libs"
    fi
    AC_SUBST(BUILD_PORTALS)
    AC_SUBST(PORTALS_INCS)
    AC_SUBST(PORTALS_LIBS)
])

dnl vim: set ft=config : 
