#
# Configure rules for CUDA
#
# Copyright (C) 2010 Seung Woo Son (sson@mcs.anl.gov)
#
# See COPYING in top-level directory.
#
AC_DEFUN([AX_CUDA],
[
    dnl Configure options for MX install path.
    dnl --with-cuda=<dir> is shorthand for
    dnl    --with-cuda-includes=<dir>/include
    dnl    --with-cuda-libs=<dir>/lib  (or lib64 if that exists)
    cuda_home=
    AC_ARG_WITH(cuda,
[  --with-cuda=<dir>         Location of the CUDA install (default no CUDA)],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-cuda requires the path to the CUDA install.])
	elif test "$withval" != no ; then
	    cuda_home="$withval"
	fi
    )
    AC_ARG_WITH(cuda-includes,
[  --with-cuda-includes=<dir>
                          Location of the MX includes],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-cuda-includes requires path to CUDA headers.])
	elif test "$withval" != no ; then
	    CUDA_INCDIR="$withval"
	fi
    )
    AC_ARG_WITH(cuda-libs,
[  --with-cuda-libs=<dir>    Location of the CUDA libraries],
	if test -z "$withval" -o "$withval" = yes ; then
	    AC_MSG_ERROR([Option --with-cuda-libs requires path to CUDA libraries.])
	elif test "$withval" != no ; then
	    CUDA_LIBDIR="$withval"
	fi
    )
    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-cuda dir.
    if test -n "$cuda_home" ; then
	if test -z "$CUDA_INCDIR"; then
	    CUDA_INCDIR=$cuda_home/include
	fi
	if test -z "$CUDA_LIBDIR"; then
	    CUDA_LIBDIR=$cuda_home/lib64
	    if test ! -d "$CUDA_LIBDIR" ; then
		CUDA_LIBDIR=$cuda_home/lib
	    fi
	fi
	if test "x$NVCC" = "x"; then
	   AC_MSG_ERROR(no)
	fi

    fi
    dnl If anything CUDA-ish was set, go look for header.
    if test -n "$CUDA_INCDIR$CUDA_LIBDIR" ; then
	save_cflags="$CFLAGS"
	CFLAGS="$CFLAGS -I$CUDA_INCDIR"
dnl	AC_CHECK_HEADER(myriexpress.h,,
dnl			AC_MSG_ERROR([Header myriexpress.h not found.]))
	dnl Run test is not possible on a machine that does not have a MX NIC.
	dnl Link test would work, but just check for existence.
dnl	if test ! -f $MX_LIBDIR/libmyriexpress.so ; then
dnl	    if test ! -f $MX_LIBDIR/libmyriexpress.a ; then
dnl		AC_MSG_ERROR([Neither MX library libmyriexpress.so or libmyriexpress.a found.])
dnl	    fi
dnl	fi
	BUILD_CUDA=1
	CFLAGS="$save_cflags"
    fi
    AC_SUBST(BUILD_CUDA)
    AC_SUBST(CUDA_INCDIR)
    AC_SUBST(CUDA_LIBDIR)

    if test -n "$BUILD_CUDA" ; then
        dnl Check for existence of mx_decompose_endpoint_addr2
        save_ldflags="$LDFLAGS"
        LDFLAGS="-L$CUDA_LIBDIR $LDFLAGS"
	save_libs="$LIBS"
	LIBS="-lcudart -lcutil $LIBS"
        save_cflags="$CFLAGS"
        CFLAGS="$CFLAGS -I$CUDA_INCDIR"

        LDFLAGS="$save_ldflags"
        CFLAGS="$save_cflags"
        LIBS="$save_libs"
    fi
])

dnl vim: set ft=config :
