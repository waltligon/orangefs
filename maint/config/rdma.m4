AC_DEFUN([AX_RDMA],
[
    dnl Configure options for RDMA install path.
    dnl --with-rdma=<dir> is shorthand for
    dnl    --with-rdma-includes=<dir>/includ
    dnl    --with-rdma-libs=<dir>/lib (or lib64 if that exists)
    rdma_home=
    AC_ARG_WITH(
        rdma,
        AS_HELP_STRING([--with-rdma=<dir>],
                       [Location of the RDMA installation (default no RDMA)]),
        [ if test -z "$withval" -o "$withval" = yes; then
              AC_MSG_ERROR([Option --with-rdma requires the path to your RDMA tree.])
          elif test "$withval" != no; then
              rdma_home="$withval"
          fi
        ])

    AC_ARG_WITH(
        rdma-includes,
        AS_HELP_STRING([--with-rdma-includes=<dir>],
                       [Location of the RDMA includes]),
        [ if test -z "$withval" -o "$withval" = yes; then
              AC_MSG_ERROR([Option --with-rdma-includes requires path to RDMA headers.])
          elif test "$withval" != no; then
              RDMA_INCDIR="$withval"
          fi
        ])

    AC_ARG_WITH(
        rdma-libs,
        AS_HELP_STRING([--with-rdma-libs=<dir>],
                       [Location of the RDMA libraries]),
        [ if test -z "$withval" -o "$withval" = yes; then
              AC_MSG_ERROR([Option --with-rdma-libs requires path to RDMA libraries.])
          elif test "$withval" != no; then
              RDMA_LIBDIR="$withval"
          fi
        ])

    dnl If supplied the incls and libs explicitly, use them, else populate them
    dnl using guesses from the --with-rdma dir.
    if test -n "$rdma_home"; then
        if test -z "$RDMA_INCDIR"; then
            RDMA_INCDIR=$rdma_home/include
        fi
        if test -z "$RDMA_LIBDIR"; then
            RDMA_LIBDIR=$rdma_home/lib64
            if test ! -d "$RDMA_LIBDIR"; then
                RDMA_LIBDIR=$rdma_home/lib
            fi
        fi
    fi

    dnl If anything RDMA-ish was set, go look for header.
    if test -n "$RDMA_INCDIR$RDMA_LIBDIR"; then
        save_cppflags="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$RDMA_INCDIR"
        AC_CHECK_HEADER(
            rdma/rdma_cma.h,,
            AC_MSG_ERROR([Header rdma/rdma_cma.h not found.]))

        dnl Run test is not possible on a machine that does not have an RDMA NIC
        dnl Link test would work, but just check for existence.
        if test ! -f $RDMA_LIBDIR/librdmacm.so; then
            if test ! -f $RDMA_LIBDIR/librdmacm.a; then
                AC_MSG_ERROR([RDMA library librdmacm.so not found.])
            fi
        fi
        BUILD_RDMA=1
        CPPFLAGS="$save_cppflags"
    fi
    AC_SUBST(BUILD_RDMA)
    AC_SUBST(RDMA_INCDIR)
    AC_SUBST(RDMA_LIBDIR)

    if test -n "$BUILD_RDMA"; then
        save_ldflags="$LDFLAGS"
        LDFLAGS="-L$RDMA_LIBDIR -libverbs"
        save_cppflags="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$RDMA_INCDIR"

        dnl Check for existence of reregister event; it's somewhat new.
        AC_MSG_CHECKING(for IBV_EVENT_CLIENT_REREGISTER)
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include "infiniband/verbs." ]], [[ enum ibv_event_type x = IBV_EVENT_CLIENT_REREGISTER; ]])],[ AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_IBV_EVENT_CLIENT_REREGISTER,
                        1,
                        Define if libibverbs has reregister event) ],[ AC_MSG_RESULT(no) ])

        dnl Check for existence of experimental Dynamically Connected
        dnl Transport events.
        AC_MSG_CHECKING(for IBV_EXP_EVENT_DCT_KEY_VIOLATION)
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include "infiniband/verbs.h" ]], [[ enum ibv_event_type x = IBV_EXP_EVENT_DCT_KEY_VIOLATION; ]])],[ AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_IBV_EXP_EVENT_DCT_KEY_VIOLATION,
                        1,
                        Define if libibverbs has dct key violation event) ],[ AC_MSG_RESULT(no) ])

        AC_MSG_CHECKING(for IBV_EXP_EVENT_DCT_ACCESS_ERR)
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include "infiniband/verbs.h" ]], [[ enum ibv_event_type x = IBV_EXP_EVENT_DCT_ACCESS_ERR; ]])],[ AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_IBV_EXP_EVENT_DCT_ACCESS_ERR,
                        1,
                        Define if libibverbs has dct access error event) ],[ AC_MSG_RESULT(no) ])

        AC_MSG_CHECKING(for IBV_EXP_EVENT_DCT_REQ_ERR)
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include "infiniband/verbs.h" ]], [[ enum ibv_event_type x = IBV_EXP_EVENT_DCT_REQ_ERR; ]])],[ AC_MSG_RESULT(yes)
            AC_DEFINE(HAVE_IBV_EXP_EVENT_DCT_REQ_ERR,
                        1,
                        Define if libibverbs has dct request error event) ],[ AC_MSG_RESULT(no) ])

        LDFLAGS="$save_ldflags"
        CPPFLAGS="$save_cppflags"
    fi
])

dnl vim: set ft=config :
