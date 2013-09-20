
AC_DEFUN([AX_CHECK_NEEDS_LIBRT],
[

AC_MSG_CHECKING([if server lib needs -lrt])
AC_TRY_LINK(
	[#include <stdlib.h>
	 #include <unistd.h>
	 #include <aio.h>],
	[lio_listio(LIO_NOWAIT, NULL, 0, NULL);],
	[AC_MSG_RESULT(no)],
	[
		oldlibs=$LIBS
		LIBS="$LIBS -lrt"
		AC_TRY_LINK(
			[#include <stdlib.h>
			 #include <unistd.h>
			 #include <aio.h>],
			[lio_listio(LIO_NOWAIT, NULL, 0, NULL);],
			[NEEDS_LIBRT=1
			 AC_SUBST(NEEDS_LIBRT)
			 AC_MSG_RESULT(yes)],
			[AC_MSG_ERROR(failed attempting to link lio_listio)])
		LIBS=$oldlibs
	])
])

AC_DEFUN([AX_CHECK_LIB_NEEDS_LIBRT],
[

AC_MSG_CHECKING([if user library needs -lrt])
AC_TRY_LINK(
	[#include <sys/mman.h>],
	[shm_open("foo", 0, 0);],
	[AC_MSG_RESULT(no)],
	[
		oldlibs=$LIBS
		LIBS="$LIBS -lrt"
		AC_TRY_LINK(
	        [#include <sys/mman.h>],
	        [shm_open("foo", 0, 0);],
			[LIB_NEEDS_LIBRT=1
			 AC_SUBST(LIB_NEEDS_LIBRT)
			 AC_MSG_RESULT(yes)],
			[AC_MSG_ERROR(failed attempting to link shm_open)])
		LIBS=$oldlibs
	])
])

