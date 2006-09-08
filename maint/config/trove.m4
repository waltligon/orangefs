AC_DEFUN([AX_CHECK_ODIRECT_AVAILABLE],
[
	  AC_MSG_CHECKING([if server supports ODIRECT])
	  
          AC_ARG_ENABLE([odirect],
            [AS_HELP_STRING([--enable-odirect],
              [support direct I/O @<:@default=check@:>@])],
            [with_odirect=$enableval],
            [with_odirect=check])
            
          if test "x$with_odirect" != xno ; then
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
		[
		if test "x$with_odirect" != xcheck; then
                 AC_MSG_FAILURE(
                   [--enable-odirect was given, but test for odirect failed])
                fi
		AC_MSG_RESULT(no)]
		,
		[
		AC_MSG_RESULT(yes)
		HAVE_TROVE_ODIRECT=1
		AC_SUBST(HAVE_TROVE_ODIRECT)
		])
	else
		AC_MSG_RESULT(no)
	fi;
	
])



AC_DEFUN([AX_CHECK_PWRITE_AVAILABLE],
[

AC_MSG_CHECKING([if server supports threaded dbpf-implementation (requires pwrite / pread)])

AC_ARG_ENABLE([pwrite],
            [AS_HELP_STRING([--enable-pwrite],
            [support threaded dbpf implementation @<:@default=check@:>@])],
            [with_pwrite=$enableval],
            [with_pwrite=check])
            
          if test "x$with_pwrite" != xno ; then
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
			[
			if test "x$with_pwrite" != xcheck; then
			AC_MSG_FAILURE(
			[--enable-pwrite was given, but test for pwrite failed])
			fi
			AC_MSG_RESULT(no)],
		[
		AC_MSG_RESULT(yes)
		HAVE_TROVE_PWRITE=1
		AC_SUBST(HAVE_TROVE_PWRITE)
		MISC_TROVE_FLAGS="$MISC_TROVE_FLAGS -D__PVFS2_HAVE_ODIRECT__"
		])
          

		
	else
		AC_MSG_RESULT(no)
	fi;

])

