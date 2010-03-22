
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
			[#include "$dbpath/include/$dbheader/db.h"],
			[DB_CFLAGS="-I$dbpath/include/$dbheader/"
			 break])
	done

	if test "x$dbheader" = "xnotfound"; then
		AC_COMPILE_IFELSE(
			[#include "$dbpath/include/db.h"],
			[DB_CFLAGS="-I$dbpath/include/"],
			[AC_MSG_FAILURE(
				Invalid libdb path specified. No db.h found.)])
	fi

        DB_LDFLAGS="-L${dbpath}/lib"
	LDFLAGS="$DB_LDFLAGS ${LDFLAGS}"

	LIBS="${oldlibs} -ldb -lpthread"
	DB_LIB="-ldb"
	CFLAGS="$DB_CFLAGS $oldcflags"
	AC_TRY_LINK(
		[#include <db.h>],
		[DB *dbp; db_create(&dbp, NULL, 0);],
		lib=db)
	CFLAGS=$oldcflags
	
    else
        for lib in db4  db3  db  notfound; do
           LIBS="${oldlibs} -l$lib -lpthread"
           DB_LIB="-l$lib"
           AC_TRY_LINK(
                  [#include <db.h>],
                  [DB *dbp; db_create(&dbp, NULL, 0);],
                  [break])
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
    dnl       AC_TRY_COMPILE([
    dnl       #include <db.h>
    dnl       ], [
    dnl       #if DB_VERSION_MAJOR < 4
    dnl              #error "DB_VERSION_MAJOR < 4; need newer Berkeley DB implementation"
    dnl       #endif
    dnl       ], AC_MSG_RESULT(yes),
    dnl       AC_MSG_RESULT(no)
    dnl              AC_MSG_ERROR(Need newer (4.x.x or later) version of Berkeley DB.
    dnl try: http://www.sleepycat.com/download/index.shtml
    dnl or: /parl/pcarns/rpms/db4-4.0.14-1mdk.src.rpm (to build rpm))
    dnl       )
    
    dnl Test to check for DB_ENV variable to error callback fn.  Then
    dnl test to see if third parameter must be const (related but not 
    dnl exactly the same).
    AC_MSG_CHECKING(for dbenv parameter to DB error callback function)
    oldcflags=$CFLAGS
    CFLAGS="$USR_CFLAGS $DB_CFLAGS -Werror"
    AC_TRY_COMPILE([
    #include <db.h>
    
    void error_callback_fn(const DB_ENV *dbenv,
                           const char *prefix,
                           const char *message)
    {
        return;
    }
    ], [
    DB *db;
    
    db->set_errcall(db, error_callback_fn);
    ], AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_DBENV_PARAMETER_TO_DB_ERROR_CALLBACK, 1,
    Define if DB error callback function takes dbenv parameter)
    have_dbenv_parameter_to_db_error_callback=yes,
    AC_MSG_RESULT(no)
    have_dbenv_parameter_to_db_error_callback=no)
    
    if test "x$have_dbenv_parameter_to_db_error_callback" = "xyes" ; then
        dnl Test if compilation succeeds without const; we expect that it will
        dnl not.
        dnl NOTE: still using -Werror!
        AC_MSG_CHECKING(if third parameter to error callback function is const)
        AC_TRY_COMPILE([
        #include <db.h>
        
        void error_callback_fn(const DB_ENV *dbenv,
                               const char *prefix,
                               char *message)
        {
            return;
        }
        ], [
        DB *db;
        
        db->set_errcall(db, error_callback_fn);
        ], AC_MSG_RESULT(no),
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_CONST_THIRD_PARAMETER_TO_DB_ERROR_CALLBACK, 1,
        Define if third param (message) to DB error callback function is const))
    fi
    
    CFLAGS="$USR_CFLAGS $DB_CFLAGS -Werror"    
    dnl Test to check for unknown third param to DB stat (four params 
    dnl total).  The unknown parameter is a function ptr so that the
    dnl the user can pass in a replcaement for malloc.
    dnl Note: this is a holdover from relatively old DB implementations,
    dnl while the txnid parameter is new.  So we don't test for the old
    dnl unknown parameter if we found the new one.
    AC_MSG_CHECKING(for DB stat with malloc function ptr)
    AC_TRY_COMPILE([
      #include <db.h>
      #include <stdlib.h>
      ], [
      int ret = 0;
      DB *db = db;
      int dummy = 0;
      u_int32_t flags = 0;
        
      ret = db->stat(db, &dummy, malloc, flags);
      ], AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_UNKNOWN_PARAMETER_TO_DB_STAT, 1,
    Define if DB stat function takes malloc function ptr)
    have_db_stat_malloc=yes,
    AC_MSG_RESULT(no)
    have_db_stat_malloc=no)

    dnl Test to check for txnid parameter to DB stat (DB 4.3.xx+)
    if test "x$have_db_stat_malloc" = "xno" ; then
    
       AC_MSG_CHECKING(for txnid parameter to DB stat function)
       AC_TRY_COMPILE([
       #include <db.h>
       ], [
       int ret = 0;
       DB *db = db;
       DB_TXN *txnid = txnid;
       u_int32_t flags = 0;
    
        ret = db->stat(db, txnid, NULL, flags);
        ], AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_TXNID_PARAMETER_TO_DB_STAT, 1, 
        Define if DB stat function takes txnid parameter)
        have_txnid_param_to_stat=yes,
        AC_MSG_RESULT(no)
        have_txnid_param_to_stat=no)
    
    fi
    
    dnl Test to check for txnid parameter to DB open (DB4.1+)
    AC_MSG_CHECKING(for txnid parameter to DB open function)
    AC_TRY_COMPILE([
    #include <db.h>
    ], [
    int ret = 0;
    DB *db = NULL;
    DB_TXN *txnid = NULL;
    char *file = NULL;
    char *database = NULL;
    DBTYPE type = 0;
    u_int32_t flags = 0;
    int mode = 0;
    
    ret = db->open(db, txnid, file, database, type, flags, mode);
    ], AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_TXNID_PARAMETER_TO_DB_OPEN, 1,
    Define if DB open function takes a txnid parameter),
    AC_MSG_RESULT(no))
    
    dnl check for DB_DIRTY_READ (it is not in db-3.2.9, for example)
    AC_MSG_CHECKING(for DB_DIRTY_READ flag)
    AC_TRY_COMPILE([
    #include <db.h>
    ], [
    u_int32_t flags = DB_DIRTY_READ;
    ], AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_DB_DIRTY_READ, 1, [Define if db library has DB_DIRTY_READ flag]),
    AC_MSG_RESULT(no))

    dnl check for DB_BUFFER_SMALL (it is returned by dbp->get in db-4.4 and up)
    AC_MSG_CHECKING(for DB_BUFFER_SMALL error)
    AC_TRY_COMPILE([
    #include <db.h>
    ], [
    int res = DB_BUFFER_SMALL;
    res++;
    ], AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_DB_BUFFER_SMALL, 1, [Define if db library has DB_BUFFER_SMALL error]),
    AC_MSG_RESULT(no))

    dnl Test to check for db->get_pagesize
    AC_MSG_CHECKING(for berkeley db get_pagesize function)
    AC_TRY_COMPILE([
    #include <db.h>
    ], [
    int ret = 0;
    DB *db = NULL;
    int pagesize;
    
    ret = db->get_pagesize(db, &pagesize);
    ], AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_DB_GET_PAGESIZE, 1, [Define if DB has get_pagesize function]),
    AC_MSG_RESULT(no))
    
    CFLAGS="$oldcflags"    
])
