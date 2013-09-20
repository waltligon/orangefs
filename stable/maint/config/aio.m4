
AC_DEFUN([AX_AIO],
[
    libaiopath=ifelse([$1], ,,$1)

    if test "x$1" != "xno"; then

        AC_MSG_CHECKING([for libaio library])
    
        if test "x${libaiopath}" != "x"; then
            CFLAGS="${CFLAGS} -I${libaiopath}/include"
            LDFLAGS="$LDFLAGS -L${libaiopath}/lib64 -L${libaiopath}/lib"
            SERVER_LDFLAGS="$SERVER_LDFLAGS -L${libaiopath}/lib64 -L${libaiopath}/lib"
        fi
        LIBS="$LIBS -laio"
    
        AC_COMPILE_IFELSE(
    	    [#include "libaio.h"],
    	    [],
    	    [AC_MSG_ERROR(Invalid libaio path specified.  No libaio.h found.)])
    
        AC_TRY_LINK(
    	    [#include "libaio.h"],
    	    [io_context_t * b;],
    	    [AC_MSG_RESULT(yes)],
    	    [AC_MSG_ERROR(could not find libaio libs)])
    
        AC_DEFINE(WITH_AIO, 1, [Define if libaio exists])
        
	AC_CHECK_HEADERS(libaio.h)
    fi
])

AC_DEFUN([AX_AIO_OPTIONAL],
[
    AC_MSG_CHECKING([for libaio library])
    TMPLIBS=${LIBS}
    LIBS="$LIBS -laio"

    AC_COMPILE_IFELSE(
      [#include "libaio.h"],
      [],
      [AC_MSG_WARN(No libaio headers found.)])

    AC_TRY_LINK(
      [#include "libaio.h"],
      [io_context_t * b;],
      [AC_MSG_RESULT(yes)
       AC_DEFINE(WITH_AIO, 1, [Define if libaio exists])
      ],
      [
      	AC_MSG_WARN(No libaio headers found.)
	LIBS=${TMPLIBS}
      ])

    AC_CHECK_HEADERS(libaio.h)

])

