
AC_DEFUN([AX_CHECK_NEEDS_LIBRT],
  [AC_MSG_CHECKING([if server lib needs -lrt])
   AC_LINK_IFELSE(
     [AC_LANG_PROGRAM([
        [#include <stdlib.h>
         #include <unistd.h>
         #include <aio.h>]],
        [lio_listio(LIO_NOWAIT, NULL, 0, NULL);])],
     [AC_MSG_RESULT(no)],
     [NEEDS_LIBRT=1
      LIB_NEEDS_LIBRT=1
      AC_SUBST(NEEDS_LIBRT)
      AC_SUBST(LIB_NEEDS_LIBRT)
      AC_MSG_RESULT(yes)]
  )
])
