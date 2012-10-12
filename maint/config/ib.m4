AC_DEFUN([AX_IB],
[
    dnl Configure options for IB install path.
    dnl --with-ib=<dir> is shorthand for
    dnl    --with-ib-includes=<dir>/include
    dnl    --with-ib-libs=<dir>/lib  (or lib64 if that exists)
    ib_home=
    AC_ARG_WITH(ib,
    [  --with-ib=<dir>         Location of the IB installation (default no IB)],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-ib requires the path to your IB tree.])
	elif test "$withval" != no ; then
	    ib_home="$withval"
	fi
    )
    AC_ARG_WITH(ib-includes,
[  --with-ib-includes=<dir>
                          Location of the IB includes],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-ib-includes requires path to IB headers.])
	elif test "$withval" != no ; then
	    IB_INCDIR="$withval"
	fi
    )
    AC_ARG_WITH(ib-libs,
[  --with-ib-libs=<dir>    Location of the IB libraries],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-ib-libs requires path to IB libraries.])
	elif test "$withval" != no ; then
	    IB_LIBDIR="$withval"
	fi
    )
    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-ib dir.
    if test -n "$ib_home" ; then
	if test -z "$IB_INCDIR"; then
	    IB_INCDIR=$ib_home/include
	fi
	if test -z "$IB_LIBDIR"; then
	    IB_LIBDIR=$ib_home/lib64
	    if test ! -d "$IB_LIBDIR" ; then
		IB_LIBDIR=$ib_home/lib
	    fi
	fi
    fi
    dnl If anything IB-ish was set, go look for header.
    if test -n "$IB_INCDIR$IB_LIBDIR" ; then
	save_cppflags="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS -I$IB_INCDIR"
	AC_CHECK_HEADER(vapi.h,, AC_MSG_ERROR([Header vapi.h not found.]))
	dnl Run test is not possible on a machine that does not have an IB NIC,
	dnl and link test is hard because we need so many little libraries.   Bail
	dnl and just check for existence; full library list is in Makefile.in.
	if test ! -f $IB_LIBDIR/libvapi.so ; then
	    if test ! -f $IB_LIBDIR/libvapi.a ; then
		AC_MSG_ERROR([Infiniband library libvapi.so not found.])
	    fi
	fi
	BUILD_IB=1
	AC_CHECK_HEADER(wrap_common.h,
	    AC_DEFINE(HAVE_IB_WRAP_COMMON_H, 1, Define if IB wrap_common.h exists.),
	    ,
	    [#include <vapi.h>])
	CPPFLAGS="$save_cppflags"
    fi
    AC_SUBST(BUILD_IB)
    AC_SUBST(IB_INCDIR)
    AC_SUBST(IB_LIBDIR)

    dnl Configure options for OpenIB install path.
    dnl --with-openib=<dir> is shorthand for
    dnl    --with-openib-includes=<dir>/include
    dnl    --with-openib-libs=<dir>/lib  (or lib64 if that exists)
    openib_home=
    AC_ARG_WITH(openib,
    [  --with-openib=<dir>     Location of the OpenIB install (default no OpenIB)],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-openib requires the path to your OpenIB tree.])
	elif test "$withval" != no ; then
	    openib_home="$withval"
	fi
    )
    AC_ARG_WITH(openib-includes,
[  --with-openib-includes=<dir>
                          Location of the OpenIB includes],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-openib-includes requires path to OpenIB headers.])
	elif test "$withval" != no ; then
	    OPENIB_INCDIR="$withval"
	fi
    )
    AC_ARG_WITH(openib-libs,
[  --with-openib-libs=<dir>
                          Location of the OpenIB libraries],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-openib-libs requires path to OpenIB libraries.])
	elif test "$withval" != no ; then
	    OPENIB_LIBDIR="$withval"
	fi
    )
    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-openib dir.
    if test -n "$openib_home" ; then
	if test -z "$OPENIB_INCDIR"; then
	    OPENIB_INCDIR=$openib_home/include
	fi
	if test -z "$OPENIB_LIBDIR"; then
	    OPENIB_LIBDIR=$openib_home/lib64
	    if test ! -d "$OPENIB_LIBDIR" ; then
		OPENIB_LIBDIR=$openib_home/lib
	    fi
	fi
    fi
    dnl If anything OpenIB-ish was set, go look for header.
    if test -n "$OPENIB_INCDIR$OPENIB_LIBDIR" ; then
	save_cppflags="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS -I$OPENIB_INCDIR"
	AC_CHECK_HEADER(infiniband/verbs.h,,
	    AC_MSG_ERROR([Header infiniband/verbs.h not found.]))
	dnl Run test is not possible on a machine that does not have an IB NIC.
	dnl Link test would work, but just check for existence.
	if test ! -f $OPENIB_LIBDIR/libibverbs.so ; then
	    if test ! -f $OPENIB_LIBDIR/libibverbs.a ; then
		AC_MSG_ERROR([OpenIB library libibverbs.so not found.])
	    fi
	fi
	BUILD_OPENIB=1
	CPPFLAGS="$save_cppflags"
    fi
    AC_SUBST(BUILD_OPENIB)
    AC_SUBST(OPENIB_INCDIR)
    AC_SUBST(OPENIB_LIBDIR)

    if test -n "$BUILD_OPENIB" ; then
	dnl Check for which version of the ibverbs library; device opening is
	dnl different.  This format is the older one, newer is
	dnl ibv_get_device_list.
	save_ldflags="$LDFLAGS"
	LDFLAGS="-L$OPENIB_LIBDIR -libverbs"
	save_cppflags="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS -I$OPENIB_INCDIR"

	AC_MSG_CHECKING(for ibv_get_devices)
	AC_TRY_LINK([], [
	    ibv_get_devices();
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_IBV_GET_DEVICES, 1,
		      Define if libibverbs has ibv_get_devices),
	    AC_MSG_RESULT(no)
	)

	dnl Check for existence of reregister event; it's somewhat new.
	AC_MSG_CHECKING(for IBV_EVENT_CLIENT_REREGISTER)
	AC_TRY_COMPILE([
	    #include "infiniband/verbs.h"
	], [
	    enum ibv_event_type x = IBV_EVENT_CLIENT_REREGISTER;
	],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_IBV_EVENT_CLIENT_REREGISTER, 1,
		      Define if libibverbs has reregister event),
	    AC_MSG_RESULT(no)
	)

	LDFLAGS="$save_ldflags"
	CPPFLAGS="$save_cppflags"
    fi
])

dnl vim: set ft=config :
