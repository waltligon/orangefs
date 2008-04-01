
AC_DEFUN([AX_OPENSSL],
[
    opensslpath=ifelse([$1], ,,$1)

    if test "x$1" != "xno"; then

        AC_MSG_CHECKING([for openssl library])
    
        if test "x${opensslpath}" != "x"; then
            CFLAGS="${CFLAGS} -I${opensslpath}/include"
            LDFLAGS="$LDFLAGS -L${opensslpath}/lib64 -L${opensslpath}/lib"
            SERVER_LDFLAGS="$SERVER_LDFLAGS -L${opensslpath}/lib64 -L${opensslpath}/lib"
        fi
        LIBS="$LIBS -lcrypto -lssl"
    
        AC_COMPILE_IFELSE(
    	    [#include "openssl/bio.h"],
    	    [],
    	    [AC_MSG_ERROR(Invalid openssl path specified.  No openssl/bio.h found.)])
    
        AC_TRY_LINK(
    	    [#include "openssl/bio.h"],
    	    [BIO * b;],
    	    [AC_MSG_RESULT(yes)],
    	    [AC_MSG_ERROR(could not find openssl libs)])
    
        AC_DEFINE(WITH_OPENSSL, 1, [Define if openssl exists])
        
	AC_CHECK_HEADERS(openssl/evp.h)
	AC_CHECK_HEADERS(openssl/crypto.h)
    fi
])

AC_DEFUN([AX_OPENSSL_OPTIONAL],
[
    AC_MSG_CHECKING([for openssl library])
    TMPLIBS=${LIBS}
    LIBS="$LIBS -lcrypto -lssl"

    AC_COMPILE_IFELSE(
      [#include "openssl/bio.h"],
      [],
      [AC_MSG_WARN(No openssl headers found.)])

    AC_TRY_LINK(
      [#include "openssl/bio.h"],
      [BIO * b;],
      [AC_MSG_RESULT(yes)
       AC_DEFINE(WITH_OPENSSL, 1, [Define if openssl exists])
      ],
      [
      	AC_MSG_WARN(No openssl headers found.)
	LIBS=${TMPLIBS}
      ])

    AC_CHECK_HEADERS(openssl/evp.h)
    AC_CHECK_HEADERS(openssl/crypto.h)

])

