
AC_DEFUN([AX_CHECK_ODIRECT_AVAILABLE],
[

AC_MSG_CHECKING([if server supports ODIRECT])

AC_TRY_LINK(
  [
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
],
[
int fd;
char * buff;
fd = open(filename, O_RDWR | O_CREAT | O_DIRECT, S_IRUSR | S_IWUSR);
close(fd);
],
	[AC_MSG_RESULT(no)],
[
AC_MSG_RESULT(yes)
HAVE_TROVE_ODIRECT=1
AC_SUBST(HAVE_TROVE_ODIRECT)
])

])

AC_DEFUN([AX_CHECK_PWRITE_AVAILABLE],
[

AC_MSG_CHECKING([if server supports pwrite])

AC_TRY_LINK(
  [
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
],
[
int fd;
char * buff;
fd = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
pwrite(fd, buff, 1024, 0);
close(fd);
],
	[AC_MSG_RESULT(no)],
[
AC_MSG_RESULT(yes)
HAVE_TROVE_PWRITE=1
AC_SUBST(HAVE_TROVE_PWRITE)
MISC_TROVE_FLAGS="$MISC_TROVE_FLAGS -D__PVFS2_HAVE_ODIRECT__"
])

])

