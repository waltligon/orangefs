AC_DEFUN([AX_BERKELEY_DB],
[
    dbpath=ifelse([$1], ,,$1)

    DB_LDFLAGS=
    dnl 
    dnl if the db is specified, try to link with -ldb
    dnl otherwise try -ldb4, then -ldb3, then -ldb
    dnl $lib set to notfound on link failure
    dnl    
    AC_MSG_CHECKING([for db library])
    oldlibs=$LIBS
    lib=notfound

    if test "x$dbpath" != "x" ; then
	oldcflags=$CFLAGS
	for dbheader in db4 db3 notfound; do
		AC_COMPILE_IFELSE(
			[AC_LANG_SOURCE([[#include "$dbpath/include/$dbheader/db.h"]])],
			[DB_CFLAGS="-I$dbpath/include/$dbheader/"
			 break])
	done

	if test "x$dbheader" = "xnotfound"; then
		AC_COMPILE_IFELSE(
			[AC_LANG_SOURCE([[#include "$dbpath/include/db.h"]])],
			[DB_CFLAGS="-I$dbpath/include/"],
			[AC_MSG_FAILURE(
				Invalid libdb path specified. No db.h found.)])
	fi

        DB_LDFLAGS="-L${dbpath}/lib"
	LDFLAGS="$DB_LDFLAGS ${LDFLAGS}"

	LIBS="${oldlibs} -ldb -lpthread"
	DB_LIB="-ldb"
	CFLAGS="$DB_CFLAGS $oldcflags"
	AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <db.h>]], [[DB *dbp; db_create(&dbp, NULL, 0);]])],[lib=db],[])
	CFLAGS=$oldcflags
	
    else
        dnl Typically a distro's db-devel package (or whatever
        dnl they might call it) includes /usr/include/db.h. 
        dnl Sometimes /usr/include/db.h is just a sym-link to
        dnl the real db.h, which might be in /usr/include/xyzzy/db.h
        dnl or somesuch. Sometimes /usr/include/db.h is a real file
        dnl that contains a single line: #include <xyzzy/db.h>.
        dnl Sometimes /usr/include/db.h is the actual db include
        dnl file. 
        dnl
        dnl In the Schrödinger’s Cat release of Fedora (relase 19),
        dnl the db-devel package (libdb4-devel-4.8.30-10.fc19.x86_64)
        dnl has no /usr/include/db.h, only /usr/include/libdb4/db.h.
        dnl
        dnl And libdb.so is not in /usr/lib(64), rather it is in
        dnl /usr/lib(64)/libdb4...
        dnl
        dnl The next few lines try to find db.h and libdb.so where ever
        dnl they might be stashed below /usr/include and /usr/lib(64)
        dnl and add those locations to CFLAGS and LDFLAGS.
	dnl
        dnl when we have rpm, we can use it to insure that the 
        dnl db.h and libdb that we find are from the same package.
        dnl If we don't have rpm, we'll just hope they're from the same
        dnl package...
        RPMPATH=`which rpm`
        if test "$RPMPATH" != ""
        then
          RPMCOMMAND="rpm -qf "
        else
          RPMCOMMAND=": # "
        fi
        DBDOTH=""
        for i in `find /usr/include -name db.h`
        do
          if test "$i" = "/usr/include/db.h"
          then
            DBDOTH=""
            break
          else
            DBDOTH="$i"
          fi
        done
        if test "$DBDOTH" != ""
        then
          DB_CFLAGS="-I `dirname $DBDOTH`"
          CFLAGS="$CFLAGS $DB_CFLAGS"
          DBPACKAGE=`$RPMCOMMAND -qf $DBDOTH`
        else
          DBPACKAGE=""
        fi

        strings /etc/ld.so.cache | grep -q /lib64
        if test "$?" = "0"
        then
          LIBPATH="/usr/lib64"
        else
          LIBPATH="/usr/lib"
        fi
      
        for i in `find "$LIBPATH" -name libdb.so`
        do
          LIBPACKAGE=`$RPMCOMMAND -qf $i`
          if test "$DBPACKAGE" = "$LIBPACKAGE"
          then
            LDFLAGS="$LDFLAGS -L"`dirname $i`
          fi
        done

        for lib in db4  db3  db  notfound; do
           LIBS="${oldlibs} -l$lib -lpthread"
           DB_LIB="-l$lib"
           AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <db.h>]], [[DB *dbp; db_create(&dbp, NULL, 0);]])],[break],[])
        done
    fi

    dnl reset LIBS value and just report through DB_LIB
    LIBS=$oldlibs 
    if test "x$lib" = "xnotfound" ; then
           AC_MSG_ERROR(could not find DB libraries)
    else
           AC_MSG_RESULT($lib)
    fi
    AC_SUBST(DB_CFLAGS)	
    AC_SUBST(DB_LIB)
    
    dnl See if we have a new enough version of Berkeley DB; needed for
    dnl    compilation of trove-dbpf component
    dnl AC_MSG_CHECKING(whether version of Berkeley DB is new enough)
    dnl       AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    dnl       #include <db.h>
    dnl       ]], [[
    dnl       #if DB_VERSION_MAJOR < 4
    dnl              #error "DB_VERSION_MAJOR < 4; need newer Berkeley DB implementation"
    dnl       #endif
    dnl       ]])],[AC_MSG_RESULT(yes)],[dnl       AC_MSG_RESULT(no)
    dnl              AC_MSG_ERROR(Need newer (4.x.x or later) version of Berkeley DB.
    dnl try: http://www.sleepycat.com/download/index.shtml
    dnl or: /parl/pcarns/rpms/db4-4.0.14-1mdk.src.rpm (to build rpm))
    dnl       ])

    oldcflags=$CFLAGS
    CFLAGS="$USR_CFLAGS $DB_CFLAGS -Werror"    
    dnl Test to check for unknown third param to DB stat (four params 
    dnl total).  The unknown parameter is a function ptr so that the
    dnl the user can pass in a replcaement for malloc.
    dnl Note: this is a holdover from relatively old DB implementations,
    dnl while the txnid parameter is new.  So we don't test for the old
    dnl unknown parameter if we found the new one.
    AC_MSG_CHECKING(for DB stat with malloc function ptr)
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
      #include <db.h>
      #include <stdlib.h>
      ]], [[
      int ret = 0;
      DB *db = db;
      int dummy = 0;
      u_int32_t flags = 0;
        
      ret = db->stat(db, &dummy, malloc, flags);
      ]])],[AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_UNKNOWN_PARAMETER_TO_DB_STAT, 1,
    Define if DB stat function takes malloc function ptr)
    have_db_stat_malloc=yes],[AC_MSG_RESULT(no)
    have_db_stat_malloc=no])

    dnl check for DB_DIRTY_READ (it is not in db-3.2.9, for example)
    AC_MSG_CHECKING(for DB_DIRTY_READ flag)
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <db.h>
    ]], [[
    u_int32_t flags = DB_DIRTY_READ;
    ]])],[AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_DB_DIRTY_READ, 1, Define if db library has DB_DIRTY_READ flag)],[AC_MSG_RESULT(no)])

    dnl check for DB_BUFFER_SMALL (it is returned by dbp->get in db-4.4 and up)
    AC_MSG_CHECKING(for DB_BUFFER_SMALL error)
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <db.h>
    ]], [[
    int res = DB_BUFFER_SMALL;
    res++;
    ]])],[AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_DB_BUFFER_SMALL, 1, Define if db library has DB_BUFFER_SMALL error)],[AC_MSG_RESULT(no)])

    dnl Check BDB version here since it's just a warning
    AC_MSG_CHECKING([Berkeley DB version])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
            #include <db.h>
        ]], [[
             #if DB_VERSION_MAJOR < 4 || \
                (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR < 8) || \
                (DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR == 8 && \
                 DB_VERSION_PATCH < 30)
                 #error "Recommend version of Berkeley DB at least 4.8.30"
             #endif
        ]])],[AC_MSG_RESULT(yes)
        HAVE_DB_OLD=0],[AC_MSG_RESULT(no)
        HAVE_DB_OLD=1
    ])
    CFLAGS="$oldcflags"    
])
