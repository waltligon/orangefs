
dnl
dnl There are two uuid libraries, one one we want, and the one we don't
dnl want.  The one we want is part of the util-linux(-ng) package, and
dnl is writen by Ted Ts'o and are compatible with OSF DCE.  Normally
dnl it's include file is in /usr/include/uuid/uuid.h and its library is
dnl in /usr/lib/libuuid.so.
dnl
dnl THe first section of this allows configure to specify the path to
dnl the installed software for non-standard installs.  The second
dnl section searches through a variety of locations for the include and
dnl library.  If future distros use alternate locations, that is where
dnl the code should be modified.
dnl
dnl Note that this code tests both the include file and library for a
dnl specific function (uuid_is_null) found only in this version of the
dnl library.  We use find to search for potential candidates and then
dnl test until a working one is found.  Finally, if these are installed
dnl from a package we warn if they (.h .so) are not from the same package.
dnl
AC_DEFUN([AX_UUID],
[
    uuidpath=ifelse([$1], ,,$1)

    oldldflags=$LDFLAGS
    oldcflags=$CFLAGS
    oldlibs=$LIBS

    UUID_LDFLAGS=""
    UUID_CFLAGS=""
    UUID_LIB=""
    dnl 
    dnl find the uuid headers and library
    dnl if user specified a path it should be in uuidpath
    dnl otherwise we will search the usual places
    dnl    
    AC_MSG_CHECKING(for UUID include file and library)

    LIBUUID="notfound"
    UUIDDOTH="notfound"

    dnl This section handles a specified path

    if test "x$uuidpath" != "x" ; then

        dnl see if the include file is pointed to by the path
        CFLAGS="-I$uuidpath/include $oldcflags"
        AC_COMPILE_IFELSE(
            [AC_LANG_SOURCE(
                [[#include <uuid/uuid.h>]],
                [[uuid_t oid;
                  uuid_is_null(oid);]]
             )],
            [   UUID_CFLAGS="-I$uuidpath/include"
                UUIDDOTH="$uuidpath/include/uuid/uuid.h"],
            [   AC_MSG_FAILURE(
                   [Invalid uuid path specified. No uuid.h found.]
                )]
        )

        LDFLAGS="-L$uuidpath/lib $oldldflags"
        LIBS="$oldlibs -luuid"

        dnl see if we can compile and link against the lib using path
        AC_LINK_IFELSE(
            [AC_LANG_PROGRAM(
                [[#include <uuid/uuid.h>]],
                [[uuid_t oid;
                  uuid_is_null(oid);]]
            )],
            [   UUID_LDFLAGS="-L$uuidpath/lib"
                LIBUUID="$uuidpath/lib/libuuid.so"
                UUID_LIB="-luuid"],
            [   AC_MSG_ERROR(
                   [Invalid uuid path specified. No libuuid.so found.]
                )]
        )
    
    else
    dnl This section sesarches standard paths

        dnl first search for include file 
        for lidir in /usr/include /usr/local/include
        do
            for path in `find $lidir -name uuid.h`
            do
                dnl see if the include file is pointed to by the path
                tmppath=`dirname $path`
                if test "x`basename $tmppath`" != "xuuid" ; then
                    continue
                fi
                CFLAGS="-I`dirname $tmppath` $oldcflags"
                AC_COMPILE_IFELSE(
                    [AC_LANG_SOURCE(
                        [[#include <uuid/uuid.h>]],
                        [[uuid_t oid;
                          uuid_is_null(oid);]]
                    )],
                    [   UUID_CFLAGS="-I`dirname $tmppath`"
                        UUIDDOTH="$path"
                    ]
                )
                if test "x$UUIDDOTH" != "xnotfound" ; then
                    break 2
                fi
            done
        done
        if test "x$UUIDDOTH" = "xnotfound" ; then
            AC_MSG_FAILURE(No suitable uuid.h found.)
        fi

        dnl next search for library 
        LIBPATH=""
        for dir in /usr/lib64 /usr/lib /lib64 /lib /usr/local/lib64 /usr/local/lib
        do
            if test -d $dir ; then
                LIBPATH="$LIBPATH $dir"
            fi
        done

        UUID_LIB="-luuid"
        LIBS="$oldlibs $UUID_LIB"
        for ludir in $LIBPATH
        do
            for path in `find $ludir ! -perm -555 -prune -o -name libuuid.so -print`
            do
                dnl see if the lib file is pointed to by the path
                LDFLAGS="-L`dirname $path` $oldldflags"
                AC_LINK_IFELSE(
                    [AC_LANG_PROGRAM(
                        [[#include <uuid/uuid.h>]],
                        [[uuid_t oid;
                          uuid_is_null(oid);]]
                     )],
                    [   UUID_LDFLAGS="-L`dirname $path`"
                        LIBUUID="$path"
                    ]
                )
                if test "x$LIBUUID" != "xnotfound" ; then
                    break 2
                fi
            done
        done
        if test "x$LIBUUID" = "xnotfound" ; then
            AC_MSG_FAILURE(No working libuuid.so found.)
        fi
    fi

    dnl check for mismatched libs and includes
    if test "x$UUIDDOTH" != "xnotfound" ; then
        UUIDPACKAGE=`rpm -qf $UUIDDOTH`
        PKRET="$?"
    else
        UUIDPACKAGE=""
    fi
    if test "x$LIBUUID" != "xnotfound" ; then
        LIBPACKAGE=`rpm -qf $LIBUUID`
        LBRET="$?"
    else
        LIBPACKAGE=""
    fi
    if test "x$PKRET" = "x0" -a "x$LBRET" = "x0" ; then
        if test "x$UUIDPACKAGE" != "x$LIBPACKAGE" ; then
            AC_MSG_WARN(UUID Library and include from different packages.)
            AC_MSG_NOTICE([$UUIDPACKAGE])
            AC_MSG_NOTICE([$LIBPACKAGE])
        fi
    fi

    dnl reset LIBS value and just report through UUID_LIB
    LIBS=$oldlibs 
    CFLAGS=$oldcflags
    LDFLAGS=$oldldflags

    dnl report result
    if test "x$LIBUUID" = "xnotfound" ; then
        AC_MSG_ERROR(could not find UUID libraries)
    fi
    if test "x$UIDDOTH" = "xnotfound" ; then
        AC_MSG_ERROR(could not find UUID libraries)
    fi
    AC_MSG_RESULT(yes)

dnl check for default locations
    if test "x$UUID_CFLAGS" = "x-I/usr/include" ; then
        UUID_CFLAGS=""
    fi
    if test "x$UUID_LDFLAGS" = "x-L$LIBPATH" ; then
        UUID_LDFLAGS=""
    fi

dnl    AC_MSG_NOTICE([UUID_CFLAGS  $UUID_CFLAGS])    
dnl    AC_MSG_NOTICE([UUID_LDFLAGS $UUID_LDFLAGS])    
dnl    AC_MSG_NOTICE([UUID_LIB     $UUID_LIB])

    AC_SUBST(UUID_CFLAGS)    
    AC_SUBST(UUID_LDFLAGS)    
    AC_SUBST(UUID_LIB)

])
