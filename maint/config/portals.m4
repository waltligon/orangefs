AC_DEFUN([AX_PORTALS],
[
    dnl
    dnl Configure to build Portals BMI method, if requested and available.
    dnl Use
    dnl   --with-portals       To find include files and libraries in standard
    dnl                        system paths.
    dnl   --with-portals=<dir> To specify a location that has include and lib
    dnl                        (or lib64) subdirectories with the goods.
    dnl Or specify the locations exactly using:
    dnl
    dnl   --with-portals-includes=<dir>
    dnl   --with-portals-libs=<dir>
    dnl
    use_portals=no
    portals_home=
    AC_ARG_WITH(portals,
    [  --with-portals[=<dir>]   Location of the Portals install (default no Portals)],
	if test -z "$withval" -o "$withval" = yes ; then
	    use_portals=yes
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
    if test -n "$PORTALS_INCDIR" ; then
	save_cppflags="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS -I$PORTALS_INCDIR"
	use_portals=yes
    fi
    if test -n "$PORTALS_LIBDIR" ; then
	save_ldflags="$LDFLAGS"
	LDFLAGS="$LDFLAGS -L$PORTALS_LIBDIR"
	use_portals=yes
    fi
    if test "$use_portals" = yes ; then
	AC_CHECK_HEADER(portals3.h,,
	    AC_MSG_ERROR([Header portals3.h not found.]))

	dnl try without first, for Cray, then try TCP version
	dnl Run test is not always possible, esp when cross-compiling or on
	dnl a box that does not have the hardware.
	linked_ok=no
	PORTALS_LIBS=
	AC_MSG_CHECKING([for portals libraries])
	AC_TRY_LINK(
	    [#include <portals3.h>],
	    [
		int m, n;
		m = PtlInit(&n);
	    ],
	    [AC_MSG_RESULT(ok); linked_ok=yes])

	if test "$linked_ok" = no ; then
	    PORTALS_LIBS="-lp3api -lp3lib -lp3utcp -lp3rt -lpthread"
	    save_libs="$LIBS"
	    LIBS="$LIBS $PORTALS_LIBS"
	    AC_TRY_LINK(
		[#include <portals3.h>],
		[
		    int m, n;
		    m = PtlInit(&n);
		],
		[AC_MSG_RESULT(ok)],
		AC_MSG_ERROR([Could not link Portals library.]))
	    BUILD_PORTALS=1
	    LIBS="$save_libs"
	fi
    fi
    if test -n "$PORTALS_INCDIR" ; then
	CPPFLAGS="$save_cppflags"
    fi
    if test -n "$PORTALS_LIBDIR" ; then
	LDFLAGS="$save_ldflags"
    fi
    AC_SUBST(BUILD_PORTALS)
    AC_SUBST(PORTALS_INCDIR)
    AC_SUBST(PORTALS_LIBDIR)
    AC_SUBST(PORTALS_LIBS)
])

dnl vim: set ft=config : 
