dnl **************************************************************
dnl PVFS2 AUTOCONF SCRIPT
dnl
dnl Process this file with autoconf to produce a configure script.
dnl You may need to use autoheader as well if changing any DEFINEs

dnl sanity checks, output header, location of scripts used here
AC_INIT(include/pvfs2-types.h)
AC_PREREQ(2.57)
AC_CONFIG_AUX_DIR(maint/config)

dnl
dnl for test builds, insert the build date and time information into
dnl into the version; for official releases, clear the PVFS2_PRE line
dnl
dnl PVFS2 versioning information.  
dnl An @ in the date string can confuse e.g. scp and 'make kmod'
CONFIGURE_TIME=`date -u +"%Y-%m-%d-%H%M%S"`
PVFS2_VERSION_MAJOR=2
PVFS2_VERSION_MINOR=9
PVFS2_VERSION_SUB=3
PVFS2_VERSION_RELEASE="orangefs"
#PVFS2_PRE=""
#PVFS2_VERSION_PRE="orangefs-$CONFIGURE_TIME"

PVFS2_VERSION=$PVFS2_VERSION_MAJOR.$PVFS2_VERSION_MINOR.$PVFS2_VERSION_SUB-$PVFS2_VERSION_RELEASE$PVFS2_VERSION_PRE
AC_SUBST(PVFS2_VERSION)
AC_DEFINE_UNQUOTED(PVFS2_VERSION_MAJOR, $PVFS2_VERSION_MAJOR, major version number)
AC_DEFINE_UNQUOTED(PVFS2_VERSION_MINOR, $PVFS2_VERSION_MINOR, minor version number)
AC_DEFINE_UNQUOTED(PVFS2_VERSION_SUB, $PVFS2_VERSION_SUB, sub version number)
#AC_DEFINE_UNQUOTED(PVFS2_VERSION_RELEASE, $PVFS2_VERSION_RELEASE, release version number)
AC_SUBST(PVFS2_VERSION_MAJOR)
AC_SUBST(PVFS2_VERSION_MINOR)
AC_SUBST(PVFS2_VERSION_SUB)
#AC_SUBST(PVFS2_VERSION_RELEASE)

dnl BMI_ONLY_TAG

dnl Checks for host type
dnl Ensures we can compile on this OS
AC_CANONICAL_HOST

USR_CFLAGS=$CFLAGS
if test "x$USR_CFLAGS" = "x"; then
	USR_CFLAGS_SET=no
fi

dnl if we use headers that might not be on every platform, add them here
AC_CONFIG_HEADER(pvfs2-config.h)

AC_CHECK_HEADER([pwd.h],
        [AC_DEFINE(HAVE_PWD_H, 1, Define if pwd.h exists)])
AC_CHECK_HEADER([features.h],
        [AC_DEFINE(HAVE_FEATURES_H, 1, Define if features.h exists)])
AC_CHECK_HEADER([fstab.h],
        [AC_DEFINE(HAVE_FSTAB_H, 1, Define if fstab.h exists)])
AC_CHECK_HEADER([malloc.h],
        [AC_DEFINE(HAVE_MALLOC_H, 1, Define if malloc.h exists)])
AC_CHECK_HEADER([memory.h],
        [AC_DEFINE(HAVE_MEMORY_H, 1, Define if memory.h exists)])
AC_CHECK_HEADER([mntent.h],
        [AC_DEFINE(HAVE_MNTENT_H, 1, Define if mntent.h exists)])
AC_CHECK_HEADER([netdb.h],
        [AC_DEFINE(HAVE_NETDB_H, 1, Define if netdb.h exists)])
AC_CHECK_HEADER([stdarg.h],
        [AC_DEFINE(HAVE_STDARG_H, 1, Define if stdarg.h exists)])
AC_CHECK_HEADER([stdint.h],
        [AC_DEFINE(HAVE_STDINT_H, 1, Define if stdint.h exists)])
AC_CHECK_HEADER([stdlib.h],
        [AC_DEFINE(HAVE_STDLIB_H, 1, Define if stdlib.h exists)])
AC_CHECK_HEADER([stdio.h],
        [AC_DEFINE(HAVE_STDIO_H, 1, Define if stdio.h exists)])
AC_CHECK_HEADER([fcntl.h],
        [AC_DEFINE(HAVE_FCNTL_H, 1, Define if fcntl.h exists)])
AC_CHECK_HEADER([utime.h],
        [AC_DEFINE(HAVE_UTIME_H, 1, Define if utime.h exists)])
AC_CHECK_HEADER([string.h],
        [AC_DEFINE(HAVE_STRING_H, 1, Define if string.h exists)])
AC_CHECK_HEADER([strings.h],
        [AC_DEFINE(HAVE_STRINGS_H, 1, Define if strings.h exists)])
AC_CHECK_HEADER([assert.h],
        [AC_DEFINE(HAVE_ASSERT_H, 1, Define if assert.h exists)])
AC_CHECK_HEADER([libgen.h],
        [AC_DEFINE(HAVE_LIBGEN_H, 1, Define if libgen.h exists)])
AC_CHECK_HEADER([dirent.h],
        [AC_DEFINE(HAVE_DIRENT_H, 1, Define if dirent.h exists)])
AC_CHECK_HEADER([time.h],
        [AC_DEFINE(HAVE_TIME_H, 1, Define if time.h exists)])
AC_CHECK_HEADER([dlfcn.h],
        [AC_DEFINE(HAVE_DLFCN_H, 1, Define if dlfcn.h exists)])

AC_CHECK_HEADER([linux/types.h],
        [AC_DEFINE(HAVE_LINUX_TYPES_H, 1, Define if linux/types.h exists)])
AC_CHECK_HEADER([linux/malloc.h],
        [AC_DEFINE(HAVE_LINUX_MALLOC_H, 1, Define if linux/malloc.h exists)])

AC_CHECK_HEADER([selinux/selinux.h],
        [AC_DEFINE(HAVE_SELINUX_H, 1, Define if selinux/selinux.h exists)])

dnl Kernel Headers checked in maint/kernel.m4

AC_CHECK_HEADER([sys/vfs.h],
		[AC_DEFINE(HAVE_SYS_VFS_H, 1, Define if sys/vfs.h exists)])
AC_CHECK_HEADER([sys/mount.h],
		[AC_DEFINE(HAVE_SYS_MOUNT_H, 1, Define if sys/mount.h exists)])
AC_CHECK_HEADER([sys/stat.h],
        [AC_DEFINE(HAVE_SYS_STAT_H, 1, Define if sys/stat.h exists)])
AC_CHECK_HEADER([sys/types.h],
        [AC_DEFINE(HAVE_SYS_TYPES_H, 1, Define if sys/types.h exists)])
AC_CHECK_HEADER([sys/socket.h],
        [AC_DEFINE(HAVE_SYS_SOCKET_H, 1, Define if sys/socket.h exists)])
AC_CHECK_HEADER([sys/sendfile.h],
        [AC_DEFINE(HAVE_SYS_SENDFILE_H, 1, Define if sys/sendfile.h exists)])
AC_CHECK_HEADER([sys/xattr.h],
        [AC_DEFINE(HAVE_SYS_XATTR_H, 1, Define if sys/xattr.h exists)])
AC_CHECK_HEADER([sys/statvfs.h],
        [AC_DEFINE(HAVE_SYS_STATVFS_H, 1, Define if sys/statvfs.h exists)])
AC_CHECK_HEADER([sys/uio.h],
        [AC_DEFINE(HAVE_SYS_UIO_H, 1, Define if sys/uio.h exists)])
AC_CHECK_HEADER([sys/mman.h],
        [AC_DEFINE(HAVE_SYS_MMAN_H, 1, Define if sys/mman.h exists)])
AC_CHECK_HEADER([sys/time.h],
        [AC_DEFINE(HAVE_SYS_TIME_H, 1, Define if sys/time.h exists)])

AC_CHECK_HEADER([arpa/inet.h],
        [AC_DEFINE(HAVE_ARPA_INET_H, 1, Define if arpa/inet.h exists)])
AC_CHECK_HEADER([attr/xattr.h],
        [AC_DEFINE(HAVE_ATTR_XATTR_H, 1, Define if attr/xattr.h exists)])

AC_CHECK_SIZEOF([long int])

dnl Check for utilities that we need during the build process
AC_PROG_INSTALL
AC_PROG_CC

AC_MSG_CHECKING([for required gcc])
if test "x$GCC" = "x"; then
	AC_MSG_ERROR(no)
fi

CFLAGS=$USR_CFLAGS

AC_PROG_CPP
AC_CHECK_PROG(HAVE_PERL, perl, yes, no)
if test $HAVE_PERL = "no"; then
    AC_MSG_ERROR("perl required in PATH to complete build")
fi

AC_CHECK_PROG(HAVE_FIND, find, yes, no)
if test $HAVE_FIND = "no"; then
    AC_MSG_ERROR("find required in PATH complete build")
fi

AC_CHECK_PROG(HAVE_BISON, bison, yes, no)
if test $HAVE_BISON = "no"; then
    AC_MSG_ERROR("bison required in PATH to complete build")
fi

AC_CHECK_PROG(HAVE_FLEX, flex, yes, no)
if test $HAVE_FLEX = "no"; then
    AC_MSG_ERROR("flex required in PATH to complete build")
fi

AC_MSG_CHECKING([for required Math::BigInt perl module])
perl -e "use Math::BigInt" 2>&1 > /dev/null
if test $? != 0; then
	AC_MSG_ERROR(no)
else
	AC_MSG_RESULT(yes)
fi

if test $host != $build; then
	AC_CHECK_PROGS(BUILD_CC, gcc cc)
else
	BUILD_CC=$CC
	BUILD_CFLAGS=$BUILD_CFLAGS
	BUILD_LDFLAGS=$BUILD_LDFLAGS
fi

AC_SUBST(BUILD_CC)
AC_SUBST(BUILD_CFLAGS)
AC_SUBST(BUILD_LDFLAGS)

#
# Threading options.
#
# Client - thread-safe or not
#   --disable-thread-safety : chooses null or posix locking mechanism
#   For applications that know they are not multi-threaded, or choose
#   to handle locking themselves, this configure option can be used to
#   slightly decrease library-induced latency by removing the locking.
#
# Client - job threading
#   libpvfs2-threaded.{so,a} with __PVFS2_JOB_THREADED__
#   This option causes the client library to spawn an extra thread
#   to handle network communications.  In this way, it is possible
#   that the library can handle new and completed requests from
#   the application while also interacting with the network.
#
#   These separate libraries can not be built using the existing
#   configure mechanism.  They are only built if required by the
#   kernel module helper.
#
# Server - three options:
#   trove
#     (hack Makefile.in)
#   job
#     (hack Makefile.in)
#   aio
#     --disable-aio-threaded-callbacks
#
#   Trove (storage) and job threading in the server are implemented
#   using #defines, but not exported through the configure mechanism.
#   AIO threaded callbacks are a separate thread used in conjunction
#   with trove, and can be disabled through a configure option as
#   AIO has been found to be buggy on many systems.
#
#
# Kernel module helper - job threading
#   --enable-threaded-kmod-helper
#
#   This is a special client that "helps" applications that access PVFS
#   through the kernel using normal VFS calls (e.g. "ls").  This
#   configure options builds it using __PVFS2_JOB_THREADED__ as described
#   above.  That enables a thread for the network layer and a thread
#   for the kernel interface.  This has the potential to increase the
#   rate at which concurrent operations are processed, but has the potential
#   drawback of somewhat higher overhead for a single operation and lack
#   of testing.
#
#   Enabling this options causes one of libpvfs2-threaded.{so,a} to be
#   built, depending on --enable-shared and --enable-static.  If both are
#   enabled, the shared library takes precedence.
#

dnl a mechanism to turn off threads in the client library
AC_MSG_CHECKING([for client library thread safety support])
AC_ARG_ENABLE(thread-safety,
[  --disable-thread-safety Disables thread safety in the client library],
[if test "x$enableval" = "xno" ; then
    LIBCFLAGS="$LIBCFLAGS -D__GEN_NULL_LOCKING__"
    THREAD_LIB=""
    AC_MSG_RESULT(no)
fi],
[   LIBCFLAGS="$LIBCFLAGS -D__GEN_POSIX_LOCKING__"
    THREAD_LIB="-lpthread"
    AC_MSG_RESULT(yes) ])

AC_SUBST(LIBCFLAGS)
AC_SUBST(THREAD_LIB)

dnl a mechanism to disable building the PVFS2 server
AC_ARG_ENABLE(server,
[  --disable-server        Disables building of PVFS2 server],
[if test "x$enableval" = "xyes" ; then
    BUILD_SERVER=1
    NEED_BERKELEY_DB=yes
else
    BUILD_SERVER=""
fi],
    BUILD_SERVER=1
    NEED_BERKELEY_DB=yes)
AC_SUBST(BUILD_SERVER)

dnl locate OpenSSL libraries
AX_OPENSSL

dnl check to see if the "fast" getgrouplist function is available
AX_GETGROUPLIST

dnl check for key-based security
AC_ARG_ENABLE(security-key,
[  --enable-security-key   Enables key-based security],
[if test "x$enableval" = "xyes" ; then
    ENABLE_SECURITY_KEY=1
    AC_DEFINE(ENABLE_SECURITY_KEY, 1, Define if key-based security is enabled)
else
    ENABLE_SECURITY_KEY=
fi],
[ENABLE_SECURITY_KEY=]
)
AC_SUBST(ENABLE_SECURITY_KEY)

dnl check for cert-based-security 
AC_ARG_ENABLE(security-cert,
[  --enable-security-cert  Enables certificate-based security],
[if test "x$enableval" = "xyes" ; then
    ENABLE_SECURITY_CERT=1
    AC_DEFINE(ENABLE_SECURITY_CERT, 1, Define if certificate-based security is enabled)
else
    ENABLE_SECURITY_CERT=
fi],
[ENABLE_SECURITY_CERT=]
)
AC_SUBST(ENABLE_SECURITY_CERT)

dnl check for capability cache
AC_ARG_ENABLE(capcache,
[  --disable-capcache      Disables server capability cache],
[if test "x$enableval" = "xyes" ; then
    ENABLE_CAPCACHE=1
    AC_DEFINE(ENABLE_CAPCACHE, 1, Define if capability cache is enabled)
else
    ENABLE_CAPCACHE=
fi],
[if test "x$ENABLE_SECURITY_KEY" = "x1" -o "x$ENABLE_SECURITY_CERT" = "x1"; then
    ENABLE_CAPCACHE=1
    AC_DEFINE(ENABLE_CAPCACHE, 1, Define if capability cache is enabled)
else
    ENABLE_CAPCACHE=
fi]
)
AC_SUBST(ENABLE_CAPCACHE)

dnl check for credential cache
AC_ARG_ENABLE(credcache,
[  --disable-credcache     Disables server credential cache],
[if test "x$enableval" = "xyes" ; then
    ENABLE_CREDCACHE=1
    AC_DEFINE(ENABLE_CREDCACHE, 1, Define if credential cache is enabled)
else
    ENABLE_CREDCACHE=
fi],
[if test "x$ENABLE_SECURITY_KEY" = "x1" -o "x$ENABLE_SECURITY_CERT" = "x1"; then
    ENABLE_CREDCACHE=1
    AC_DEFINE(ENABLE_CREDCACHE, 1, Define if credential cache is enabled)
else
    ENABLE_CREDCACHE=
fi]
)
AC_SUBST(ENABLE_CREDCACHE)

dnl check for certificate cache
AC_ARG_ENABLE(certcache,
[  --disable-certcache     Disables server certificate cache],
[if test "x$enableval" = "xyes" ; then
    ENABLE_CERTCACHE=1
    AC_DEFINE(ENABLE_CERTCACHE, 1, Define if certificate cache is enabled)
else
    ENABLE_CERTCACHE=
fi],
[if test "x$ENABLE_SECURITY_CERT" = "x1"; then
    ENABLE_CERTCACHE=1
    AC_DEFINE(ENABLE_CERTCACHE, 1, Define if certificate cache is enabled)
else
    ENABLE_CERTCACHE=
fi]

)
AC_SUBST(ENABLE_CERTCACHE)

if test "x$ENABLE_SECURITY_KEY" = "x1" -a "x$ENABLE_SECURITY_CERT" = "x1"; then
    AC_MSG_ERROR([only one of --enable-security-key and --enable-security-cert may be used])
fi

dnl OpenSSL is required for robust security
if test \( "x$ENABLE_SECURITY_KEY" = "x1" -o "x$ENABLE_SECURITY_CERT" = "x1" \) -a "x$OPENSSL_CPPFLAGS" = "x"; then
    AC_MSG_ERROR([OpenSSL required for robust security (use --with-ssl)])
fi

dnl credential cache only valid in security modes
if test "x$ENABLE_CREDCACHE" = "x1" -a "x$ENABLE_SECURITY_KEY" = "x" -a "x$ENABLE_SECURITY_CERT" = "x"; then
    AC_MSG_ERROR([credential cache can only be used in key- or certificate-based security mode])
fi

dnl certificate cache only valid in certificate-based security mode
if test "x$ENABLE_CERTCACHE" = "x1" -a "x$ENABLE_SECURITY_CERT" = "x"; then
    AC_MSG_ERROR([certificate cache can only be used in certificate-based security mode])
fi

dnl locate LDAP libraries
AX_LDAP

dnl certificate security requires LDAP
if test "x$ENABLE_SECURITY_CERT" = "x1" -a "x$LDAP_LIB" = "x"; then
    AC_MSG_ERROR([OpenLDAP required for certificate-based security (use --with-ldap)])
fi

dnl enable profiling
AC_ARG_ENABLE(profiling,
[  --enable-profiling      Enables simple profiling],
[if test "x$enableval" = "xyes" ; then
    ENABLE_PROFILING=1
    AC_DEFINE(ENABLE_PROFILING, 1, Define if profiling is enabled)
else
    ENABLE_PROFILING=
fi],
)
AC_SUBST(ENABLE_PROFILING)


dnl a mechanism to turn off memory usage statistics in karma (may be
dnl confusing for some classes of users)
AC_ARG_ENABLE(karma-mem-usage-stats,
[  --disable-karma-mem-usage-stats
                          Disables memory usage stats in karma],
[if test "x$enableval" = "xno" ; then
    CFLAGS="$CFLAGS -D__KARMA_DISABLE_MEM_USAGE__"
fi]
,)

dnl build only the bmi library
AC_ARG_ENABLE(bmi-only,
[  --enable-bmi-only       Build only the BMI library],
[if test "x$enableval" = "xyes" ; then
    BUILD_BMI_ONLY=1
fi]
,)
AC_SUBST(BUILD_BMI_ONLY)

dnl if we're only building the BMI lib, disable checks for server and DB.
if test "x$BUILD_BMI_ONLY" = "x1"; then
	BUILD_SERVER=""
	NEED_BERKELEY_DB=no
	AC_SUBST(BUILD_SERVER)
	AC_SUBST(NEED_BERKELEY_DB)
fi

dnl a mechanism to turn off perf counters
AC_ARG_ENABLE(perf-counters,
[  --disable-perf-counters Disables pvfs2-server performance counters],
[if test "x$enableval" = "xno" ; then
    CFLAGS="$CFLAGS -D__PVFS2_DISABLE_PERF_COUNTERS__"
fi]
,)

dnl a mechanism to turn on mmap-readahead caching (for kernel interface)
MMAP_RA_CACHE=""
AC_ARG_ENABLE(mmap-racache,
[  --enable-mmap-racache   **EXPERIMENTAL** Enables mmap-readahead in kernel 
                          interface],
[if test "x$enableval" = "xyes" ; then
MMAP_RA_CACHE="-DUSE_MMAP_RA_CACHE"
fi]
,)
AC_SUBST(MMAP_RA_CACHE)

dnl a mechanism that resets the file position pointer when an error occurs whether
dnl or not any bytes were written (kernel interface only).
RESET_FILE_POS=""
AC_ARG_ENABLE(reset-file-pos,
[  --enable-reset-file-pos Resets file position pointer in kernel interface upon error],
[if test "x$enableval" = "xyes" ; then
RESET_FILE_POS="-DRESET_FILE_POS"
fi]
,)
AC_SUBST(RESET_FILE_POS)


dnl See if the --enable-trusted-connections  option was given to configure
AC_ARG_ENABLE(trusted-connections,
[  --enable-trusted-connections
                          **EXPERIMENTAL** Enables connects only 
                          from trusted hosts/ports ],
[if test "x$enableval" = "xyes"; then
TRUSTED_CONNECTIONS="-DUSE_TRUSTED"
fi],
)
AC_SUBST(TRUSTED_CONNECTIONS)


dnl a function to check if we have the required gtk stuff for the
dnl karma gui
test_for_karma()
{
  AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, yes, no)
  if test "x$HAVE_PKGCONFIG" = "xyes" ; then
     AC_MSG_CHECKING([for gtk2.0 (for karma gui)])
     if `pkg-config --exists gtk+-2.0` ; then
        AC_MSG_RESULT(yes)
        GTKLIBS=`pkg-config --libs gtk+-2.0`
        GTKCFLAGS=`pkg-config --cflags gtk+-2.0`

        AC_SUBST(GTKLIBS)
        AC_SUBST(GTKCFLAGS)
        BUILD_KARMA="1"
        AC_SUBST(BUILD_KARMA)
     else
        AC_MSG_RESULT(no)
     fi
  fi
}

dnl optionally disable building the karma gui
AC_ARG_ENABLE(karma,
[  --disable-karma         Disables optional gui.  (Enabled by default)],
[ if test "x$enableval" = "xyes" ; then
      test_for_karma
   fi
],
[test_for_karma]
)

dnl shared or static client library (see --enable-shared below)
AC_ARG_ENABLE(static,
[  --disable-static        Do not build static client library],
[build_static=$enableval], [build_static=yes])
AC_SUBST(build_static)

dnl NOTE: removing the linux-gnu host check for now; we may want to put it back
dnl before releaseing any code, though...  -Phil
dnl 
dnl AC_MSG_CHECKING(whether we know how to compile for this OS)
dnl if test x$host_os != xlinux-gnu; then AC_MSG_ERROR($host_os not supported.)
dnl else AC_MSG_RESULT(yes)
dnl fi

dnl some redhat 2.4 kernels have a lot of 2.6 features, so we need to take
dnl special measures to accomodate those hacked kernels.  However, we do not
dnl need to take these workarounds if someone is building a stock kernel.org
dnl kernel.  

dnl was the configure option passed in?
set_redhat24=0
dnl what was the configure option set to?
use_redhat24=0

AC_ARG_ENABLE([redhat24],
[  --enable-redhat24       Enables workaround for RedHat 2.4 kernel], 
    set_redhat24=1
    if test "$enableval" = no ; then 
    	use_redhat24=0
    else
        use_redhat24=1 
    fi
)

dnl some systems (ie, RedHat EL 3 prior to Update 2) have buggy NPTL/Pthread
dnl implementations.  We accomodate this with a server stub script hack that
dnl attempts to disable NPTL and fall back to old fashioned Linux Threads.

dnl was the configure option passed in?
set_nptl_workaround=0
dnl what was the configure option set to?
use_nptl_workaround=0

AC_ARG_ENABLE([nptl-workaround],
[  --enable-nptl-workaround
                          Enables workaround for buggy NPTL/Pthread libraries], 
    set_nptl_workaround=1
    if test "$enableval" = no ; then 
    	use_nptl_workaround=0
    else
        use_nptl_workaround=1 
    fi
)

dnl We check for the following redhat tags, as they all have similarly
dnl modified 2.4.x kernels
dnl
RHTAG1="Red Hat Linux release 9"
RHTAG2="Red Hat Enterprise Linux WS release 3"
RHTAG3="Red Hat Enterprise Linux AS release 3"
RHTAG4="Red Hat Enterprise Linux ES release 3"
RHTAG5="Red Hat Linux Advanced Server release 2.1AS"
RHTAG6="Fedora Core release 1 (Yarrow)"


dnl we won't be able to catch all cases, but see if we can
dnl get the majority of platforms where we might need to work around
dnl backported features.  Basically we give up if running on these redhat
dnl versions 

REDHAT_RELEASE=""
if test -f /etc/redhat-release; then
    AC_MSG_CHECKING(for tagged Redhat releases (must patch))
    REDHAT_REL=`cat /etc/redhat-release`
    if test "x`echo $REDHAT_REL | cut -b 1-23`" = "x$RHTAG1" ||
       test "x`echo $REDHAT_REL | cut -b 1-37`" = "x$RHTAG2" ||
       test "x`echo $REDHAT_REL | cut -b 1-37`" = "x$RHTAG3" ||
       test "x`echo $REDHAT_REL | cut -b 1-37`" = "x$RHTAG4" ||
       test "x`echo $REDHAT_REL | cut -b 1-43`" = "x$RHTAG5" ||
       test "x`echo $REDHAT_REL | cut -b 1-30`" = "x$RHTAG6" ; then
       AC_MSG_RESULT(yes)
       
       if test "$set_redhat24" = 0 || test "$set_nptl_workaround" = 0 ; then
           AC_MSG_ERROR([
    You appear to be configuring PVFS2 on a RedHat distribution that
    likely ships with a heavily modified kernel and c library.  You must
    specify two configure arguments to provide necessary information before
    proceeding.  First pick one of these two options:
    =============================
    --enable-redhat24         (if you are using a redhat provided 2.4 kernel)
    --disable-redhat24        (if you are using a stock kernel.org kernel)
    =============================
    Also pick one of the next two options.  You should probably enable the
    workaround if you are using RedHat EL 3 prior to update 2.  Otherwise it
    is probably safer (and higher performance) to disable it:
    =============================
    --enable-nptl-workaround  (to work around buggy glibc pthread library)
    --disable-nptl-workaround (if you trust your glibc pthread library)
    ============================= ])
       fi
    else
       AC_MSG_RESULT(no)
    fi
fi

dnl some non-redhat kernels (like whitebox linux and centos) rebuild
dnl rhel and give the distribution a different name, so if the user sets
dnl "enable-redhat24", then use it no matter what /etc/redhat-release might say

if test "$use_redhat24" = 1 ; then
   REDHAT_RELEASE="-DREDHAT_RELEASE_9"
fi
AC_SUBST(REDHAT_RELEASE)

if test "$use_nptl_workaround" = 1 ; then
   NPTL_WORKAROUND="1"
fi
AC_SUBST(NPTL_WORKAROUND)

use_aio_thcb=1
AC_ARG_ENABLE([aio-threaded-callbacks],
[  --disable-aio-threaded-callbacks
                          **EXPERIMENTAL** Disable use of AIO 
                          threaded callbacks],
    if test "$enableval" = no ; then use_aio_thcb=0 ; fi)

dnl there used to be a big hairy test in here, back when glibc-2.3.0 and
dnl glibc-2.3.1 had buggy aio callbacks.  That test was broken because it
dnl assumed just linux, and could not handle glibc-2.4.x (or newer).  Rely on
dnl aio-threaded-callbacks, perhaps with a blacklist of distros that have the
dnl broken glibc.

MISC_TROVE_FLAGS=""
if test $use_aio_thcb = 1 ; then
	MISC_TROVE_FLAGS="-D__PVFS2_TROVE_AIO_THREADED__"
fi
AC_SUBST(MISC_TROVE_FLAGS)

dnl Check for AIO's aiocb->__error_code field (linux has it, OS X doesn't)
AC_MSG_CHECKING([for __error_code field in aiocb struct])
AC_TRY_COMPILE(
    [
     	#include <aio.h>
    ], 
    [
    	struct aiocb aiocb;
	aiocb.__error_code = 0;
    ],
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_AIOCB_ERROR_CODE, 1, Define if aiocb->__error_code exists)
    ,
    AC_MSG_RESULT(no)
)

dnl Check for AIO's aiocb->__return_value field (linux has it, OS X doesn't)
AC_MSG_CHECKING([for __return_value field in aiocb struct])
AC_TRY_COMPILE(
    [
        #include <aio.h>
    ],
    [
        struct aiocb aiocb;
	aiocb.__return_value = 0;
    ],
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_AIOCB_RETURN_VALUE, 1, Define if aiocb->__return_value exists)
    ,
    AC_MSG_RESULT(no)
)

dnl Check for updated selinux so it won't break usrint
AC_MSG_CHECKING([for const security_context_t in setfilecon])
old_cflags="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_TRY_COMPILE(
    [
        #include <stdio.h>
        #include <selinux/selinux.h>
        int setfilecon(const char *path, const char *con)
        {
            return 0;
        }
    ],
    [
        int ret;
        const char *sc_t = NULL;
	    ret = setfilecon("somepath", sc_t);
    ],
    [
       AC_MSG_RESULT(yes)
       AC_DEFINE(HAVE_CONST_SECURITY_CONTEXT, 1, Define if setfilecon uses a const security_context_t)
    ],
    AC_MSG_RESULT(no)
)
CFLAGS="$old_cflags"

dnl Check byte ordering
AC_C_BIGENDIAN

dnl Check size of pointer type; needed by id-generator
AC_CHECK_SIZEOF(void *)

dnl Options for debugging
dnl This particular one enables tracing of memory allocation and
dnl    freeing with the mtrace tool
AC_ARG_WITH(mtrace,
[  --with-mtrace           Use mtrace (must set MALLOC_TRACE to output file)],
CFLAGS="$CFLAGS -include mcheck.h"
AC_DEFINE(WITH_MTRACE, 1, Define if mtrace memory leak detection was enabled)
)

dnl This particular one enables berkeley db to emit detected errors
AC_ARG_WITH(berkdb-debug,
[  --with-berkdb-debug     Use berkeley db error reporting (if detected).],
AC_DEFINE(BERKDB_ERROR_REPORTING, 1, Define if berkeley db error reporting was enabled)
)

TAU_INCS=
BUILD_TAU=

dnl use the tau trace library
AC_ARG_WITH(tau,
[  --with-tau=path         Use TAU trace library installed in "path"],
    if test "x$withval" = "xyes" ; then
           AC_MSG_ERROR(--with-tau must be given a pathname)
    else
       TAU_INCS="-I$withval/include"
       CFLAGS="$CFLAGS $TAU_INCS -D__PVFS2_ENABLE_EVENT__"
       LDFLAGS="$LDFLAGS -L$withval/$(uname -m)/lib"
       tau_mkfile=$withval/$(uname -m)/lib/Makefile.*-profile-trace
       if test ! -f $tau_mkfile; then
           AC_MSG_ERROR(TAU not compiled with profiling and tracing support)
       fi
       tau_config=$(echo $tau_mkfile | sed -e "s|.*Makefile.tau-||")
       LIBS="$LIBS -lTAU_tf -lTAU_traceinput-$tau_config -ltau-$tau_config -lpthread -lstdc++"
       AC_DEFINE(HAVE_TAU, 1, [Define if TAU library is used])
       BUILD_TAU=1
    fi
)

AC_SUBST(TAU_INCS)
AC_SUBST(BUILD_TAU)

BUILD_KERNEL=

dnl
dnl Enables the kernel module to build if the appropriate
dnl linux-2.6.x path is specified
dnl Make sure this is a usable kernel source tree too.
dnl version.h moved with kernel 3.7, so check for it in the 
dnl new place too.
dnl
AC_ARG_WITH(kernel,
[  --with-kernel=srcpath   Build pvfs2 kernel module against 2.6.x src],
    if test "x$withval" = "xyes" -o "x$withval" = "x" ; then
           AC_MSG_ERROR(--with-kernel must be given the path to your kernel source.)
    fi
    if ! test -d $withval ; then
           AC_MSG_ERROR(The --with-kernel path $withval is not a directory.)
    fi
    if ! test -r $withval/include/linux/version.h && ! test -r $withval/include/generated/uapi/linux/version.h; then
       AC_MSG_ERROR(The kernel source tree must not have been configured.)
    fi

    if test -r $withval/include/linux/version.h && grep -q UTS_RELEASE..2\\.6\\. $withval/include/linux/version.h ; then
        vers=`sed -n '/UTS_RELEASE/{; s/.*"\([0-9]\.[0-9]\).*".*/\1/; p; }' $withval/include/linux/version.h`
    elif test -r $withval/include/linux/utsrelease.h && grep -q UTS_RELEASE..2\\.6\\. $withval/include/linux/utsrelease.h; then
        vers=`sed -n '/UTS_RELEASE/{; s/.*"\([0-9]\.[0-9]\).*".*/\1/; p; }' $withval/include/linux/utsrelease.h`
    elif test -r  $withval/include/generated/utsrelease.h && grep -q UTS_RELEASE..2\\.6\\. $withval/include/generated/utsrelease.h; then
        vers=`sed -n '/UTS_RELEASE/{; s/.*"\([0-9]\.[0-9]\).*".*/\1/; p; }' $withval/include/generated/utsrelease.h`
    elif test -r  $withval/include/generated/utsrelease.h && grep -qE UTS_RELEASE..3\\. $withval/include/generated/utsrelease.h; then
        vers=`sed -n '/UTS_RELEASE/{; s/.*"\([0-9]\.[0-9]\).*".*/\1/; p; }' $withval/include/generated/utsrelease.h`
    elif test -r  $withval/include/generated/utsrelease.h && grep -qE UTS_RELEASE..4\\. $withval/include/generated/utsrelease.h; then
        vers=`sed -n '/UTS_RELEASE/{; s/.*"\([0-9]\.[0-9]\).*".*/\1/; p; }' $withval/include/generated/utsrelease.h`
    else
        AC_MSG_ERROR(The kernel source tree does not appear to be 2.6 or 3.X or 4.X)
        
    fi

    # At least up through 2.6.3 needed to write .__modpost.cmd; this changed
    # sometime between then and 2.6.10.   Now anybody can compile out-of-tree
    # modules against a configured kernel tree
    LINUX_KERNEL_SRC="$withval"
    BUILD_KERNEL=1
    , LINUX_KERNEL_SRC=""
)

dnl
dnl Enables the kernel module to build if the appropriate
dnl linux-2.4.x path is specified
dnl Make sure this is a usable kernel source tree too.
dnl
AC_ARG_WITH(kernel24,
[  --with-kernel24=srcpath Build pvfs2 kernel module against 2.4.x src],
    if test "x$withval" = "xyes" -o "x$withval" = "x" ; then
           AC_MSG_ERROR(--with-kernel24 must be given the path to your kernel 2.4.x source.)
    fi
    if ! test -d $withval ; then
           AC_MSG_ERROR(The --with-kernel path $withval is not a directory.)
    fi
    if ! test -r $withval/include/linux/version.h ; then
       AC_MSG_ERROR(The kernel source tree must have been configured.)
    fi
    vers=`sed -n '/UTS_RELEASE/{; s/.*"\([0-9]\.[0-9]\).*".*/\1/; p; }' $withval/include/linux/version.h`
    if ! grep -q UTS_RELEASE..2\\.4\\. $withval/include/linux/version.h ; then
       AC_MSG_ERROR(The kernel source tree does not appear to be 2.4)
    fi
    k24_minor_ver=`echo $vers | cut -b 26- | sed -n 's/\"//p' | head -n 1`
    tmp_k24_minor_ver=`echo $k24_minor_ver | grep \-`
    if ! test "x$tmp_k24_minor_ver" = "x"; then
       tmp_k24_minor_ver=`echo $k24_minor_ver | cut -d- -f1`
       k24_minor_ver=$tmp_k24_minor_ver
    fi
    LINUX24_KERNEL_SRC="$withval" LINUX24_KERNEL_MINOR_VER="`echo $k24_minor_ver| cut -d'.' -f 1`"
    BUILD_KERNEL=1
    , LINUX24_KERNEL_SRC="" LINUX24_KERNEL_MINOR_VER="")

AC_SUBST(BUILD_KERNEL)

dnl now that we have the path to kernel source we can feature-test kernels. 

oldcflags=$CFLAGS
if test -z "${LINUX_KERNEL_SRC}"  ; then
	lk_src=${LINUX24_KERNEL_SRC}
elif test -z "${LINUX24_KERNEL_SRC}"  ; then
	lk_src=${LINUX_KERNEL_SRC}
fi

dnl some kernels (SLES9 2.6.5-7.191, for one) will pass the test for ki_dtor
dnl but the resulting pvfs2.ko module will be built with internal
dnl symbols (mmgrab and flush_icache_range).  
dnl 
dnl if option not given, carry out the test.  Only certain kernels (like SLES
dnl 9) will need --disable-kernel-aio

AC_ARG_ENABLE(kernel-aio,
  [  --disable-kernel-aio    Forcibly disable kernel aio],
  [ enable_kernel_aio=$enableval ],
  [ enable_kernel_aio=yes ]
  )

AC_ARG_ENABLE(kernel-sendfile,
  [  --enable-kernel-sendfile
                          **EXPERIMENTAL** Forcibly enables kernel sendfile],
  [ enable_kernel_sendfile=$enableval ],
  [ enable_kernel_sendfile=no ]
  )

if test -n "$lk_src" ; then
    AX_KERNEL_FEATURES
    AC_DEFINE(WITH_LINUX_KMOD, 1, [Define to build for linux kernel module userspace helper.])
fi

dnl
dnl Enabling this option links pvfs2-client-core against libpvfs2-threaded.so.
dnl Note that even without this option, pvfs2-client-core always requires
dnl pthreads to run its remount thread.
dnl
AC_ARG_ENABLE([threaded-kmod-helper],
[  --enable-threaded-kmod-helper
                          **EXPERIMENTAL** Use threads in the kernel 
                          helper application],
[ if test "x$enableval" = "xyes" ; then
    THREADED_KMOD_HELPER=yes
  fi
])
AC_SUBST(THREADED_KMOD_HELPER)

dnl PAV configuration needs absolute location of source and build.
dnl Linux-2.6 module needs absolute location of source, and uses the
dnl relative location for soft links for out-of-tree builds.
BUILD_ABSOLUTE_TOP=`pwd`
SRC_RELATIVE_TOP=`echo $0 | sed -e "s|configure$||"`
SRC_ABSOLUTE_TOP=`cd $SRC_RELATIVE_TOP ; pwd`

AC_SUBST(LINUX_KERNEL_SRC)
AC_SUBST(LINUX24_KERNEL_SRC)
AC_SUBST(LINUX24_KERNEL_MINOR_VER)
AC_SUBST(BUILD_ABSOLUTE_TOP)
AC_SUBST(SRC_RELATIVE_TOP)
AC_SUBST(SRC_ABSOLUTE_TOP)

dnl Optimized code, but never override CFLAGS from command line or environment.
AC_ARG_ENABLE(fast,
[  --enable-fast           Disables optional debugging, enables optimization],
[
if test "x$USR_CFLAGS_SET" = "xno"; then
    CFLAGS="$CFLAGS -DNDEBUG -O3 -DGOSSIP_DISABLE_DEBUG"
fi
])

dnl Disable compiler optimization
AC_ARG_ENABLE(opt,
[  --disable-opt           Disables compiler optimization],
[if test "x$enableval" = "xyes" ; then
    CC_OPT=-O2
else
    CC_OPT=-O0
fi],
    CC_OPT=-O2)
AC_SUBST(CC_OPT)

dnl
dnl a function to check for FUSE
dnl
test_for_fuse()
{
  AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, yes, no)
  if test "x$HAVE_PKGCONFIG" = "xyes" ; then
    AC_MSG_CHECKING([for FUSE library])
    if `pkg-config --exists fuse` ; then
       AC_MSG_RESULT(yes)
       FUSE_LDFLAGS=`pkg-config --libs fuse`
       FUSE_CFLAGS=`pkg-config --cflags fuse`

       AC_SUBST(FUSE_LDFLAGS)
       AC_SUBST(FUSE_CFLAGS)
       BUILD_FUSE="1"
       AC_SUBST(BUILD_FUSE)
    else
            AC_MSG_ERROR([FUSE: FUSE library not found. Check LD_LIBRARY_PATH.])
    fi
  else
          AC_MSG_ERROR(FUSE: pkg-config not available. Please install pkg-config.)
  fi
}

dnl FUSE component
AC_ARG_ENABLE(fuse,
[  --enable-fuse           **EXPERIMENTAL** Enables FUSE component],
[
if test "x$enableval" = "xyes" ; then
   test_for_fuse
   CFLAGS="$CFLAGS -D__PVFS2_ENABLE_FUSE__"
fi
],
)

# default CFLAGS is -g -O2, unless user set CFLAGS or asked for --enable-fast
if test "x$USR_CFLAGS_SET" = "xno" && test "x$enable_fast" != "xyes"; then
    CFLAGS="$CFLAGS -g $CC_OPT"
fi


dnl Options that are not yet working
dnl --------------------------------
dnl AC_ARG_ENABLE(profiling,
dnl [  --enable-profiling      Enables profiling (using gprof).                     *** not yet working ***],
dnl CFLAGS="$CFLAGS -pg -fprofile-arcs",
dnl )
dnl 
dnl AC_ARG_ENABLE(coverage,
dnl [  --enable-coverage       Enables coverage analysis (disables optimizations).  *** not yet working ***],
dnl [ CFLAGS="$CFLAGS -g -pg -ftest-coverage -O0"
dnl ENABLE_COVERAGE=1 ],
dnl )

AC_SUBST(ENABLE_COVERAGE)

STRICT_CFLAGS=
AC_ARG_ENABLE(strict,
[  --enable-strict         Turn on strict compiler warnings],
    STRICT_CFLAGS=1)
AC_SUBST(STRICT_CFLAGS)

dnl Check if user actually wants to see all our build output (compiles, links, etc.).
AC_ARG_ENABLE(verbose-build,
[  --enable-verbose-build  Enables full output during build process],
QUIET_COMPILE=0,
QUIET_COMPILE=1)

AC_SUBST(QUIET_COMPILE)

dnl
dnl a function to check for epoll capabilities
dnl
test_for_epoll()
{
  AC_CHECK_HEADERS(sys/epoll.h)
  AC_MSG_CHECKING(for epoll functions)
  AC_TRY_LINK([#include <sys/epoll.h>], [
	int fd;
	fd = epoll_create(1);
	close(fd);
  ],
     AC_MSG_RESULT(yes)
     BUILD_EPOLL=1
     AC_SUBST(BUILD_EPOLL),
     AC_MSG_RESULT(no)
  )
}

dnl optionally enable use of epoll() on Linux instead of poll()
dnl optionally disable use of epoll() on Linux instead of poll()
AC_ARG_ENABLE(epoll,
[  --enable-epoll          Enables use of epoll for TCP/IP polling 
                          (epoll is enabled by default, this option
			  ensures no checking is done for x-compiling)
  --disable-epoll         Disables epoll, use poll instead],
[
   if test "x$enableval" = "xyes"; then
        BUILD_EPOLL=1
        AC_SUBST(BUILD_EPOLL)
   fi
],
[
  test_for_epoll
])

dnl enables a hack to print back traces out of segfault signal handler
AC_ARG_ENABLE(segv-backtrace,
[  --disable-segv-backtrace
                          Disables back traces in segfault signal handler],,
    AC_MSG_CHECKING(if segv backtrace capable)
    AC_TRY_COMPILE([
#include <execinfo.h>
#define __USE_GNU
#include <ucontext.h>
#if !defined(REG_EIP) && !defined(REG_RIP)
		   choke me
#endif
		   ], [],
		   AC_MSG_RESULT(yes)
    		   PVFS2_SEGV_BACKTRACE=1,
		   AC_MSG_RESULT(no)
    )
)
AC_SUBST(PVFS2_SEGV_BACKTRACE)

dnl shared or static client library
AC_ARG_ENABLE(shared,
    [  --enable-shared         Build shared client library],
    [build_shared=$enableval],
    [build_shared=no])
AC_SUBST(build_shared)

dnl Bad things happen in the makefile if you happen to --disable-static
dnl but do not --enable-shared.
if test "x$build_shared" = "xno" -a "x$build_static" = "xno" ; then
    AC_MSG_ERROR([Must do --enable-shared or --enable-static or both.])
fi

dnl shared or static client library
AC_ARG_ENABLE(threaded,
    [  --disable-threaded      Do not build threaded client library],
    [if "x$THREADED_KMOD_HELPER = xyes" ; then
         build_threaded=yes
         AC_MSG_WARN([Ignoring --disable-threaded in favor of --enable-threaded-kmod-heaper.])
     else
         build_threaded=$enableval
     fi
    ],
    [build_threaded=yes])
AC_SUBST(build_threaded)

dnl Method to disable build of user interface libraries
BUILD_USRINT=
AC_ARG_ENABLE(usrint,
   [  --disable-usrint        Do not build posix and stdio user libraries],
   [if test "x$enableval" = "xyes" ; then
       AC_DEFINE(PVFS_USRINT_BUILD, 1,
                 [Should we build user interface libraries.])
       BUILD_USRINT=1
    else
       AC_DEFINE(PVFS_USRINT_BUILD, 0,
                 [Should we build user interface libraries.])
    fi
    build_usrint=$enableval
   ],
   [AC_DEFINE(PVFS_USRINT_BUILD, 1,
              [Should we build user interface libraries.])
    build_usrint=yes
    BUILD_USRINT=1
   ])
AC_SUBST(build_usrint)

if test "x$build_usrint" = "xyes"; then
	dnl check if -lrt is required by usrint
	AX_CHECK_LIB_NEEDS_LIBRT
fi
 
dnl libofs is ALWAYS symbolic, others should never be
dnl
dnl Method to enable symbolic user interface libraries
dnl BUILD_SYMBOLIC=
dnl AC_ARG_ENABLE(symbolic,
dnl    [  --enable-symbolic  Build library to force programs to use libofs.so],
dnl    [if test "x$enableval" = "xyes" ; then
dnl       if test "x$build_usrint" = "xyes" -a "x$build_shared" = "xyes" ; then
dnl          AC_DEFINE(PVFS_USRINT_SYMBOLIC, 1,
dnl                [Build library to force programs to use libofs.so.])
dnl          BUILD_SYMBOLIC=1
dnl       else
dnl          AC_MSG_ERROR([Must build usrint and shared to build symbolic library.])
dnl       fi
dnl    else
dnl       AC_DEFINE(PVFS_USRINT_SYMBOLIC, 0,
dnl                 [Build library to force programs to use libofs.so.])
dnl    fi
dnl    build_symbolic=$enableval
dnl    ],
dnl    [AC_DEFINE(PVFS_USRINT_SYMBOLIC, 0,
dnl               [Build library to force programs to use libofs.so.])
dnl     build_symbolic=no
dnl    ])
dnl AC_SUBST(build_symbolic)
 
dnl Method to enable user interface kmod support
USRINT_KMOUNT=
AC_ARG_ENABLE(usrint-kmount,
[  --enable-usrint-kmount  Assume FS is mounted via kernel],
[if test "x$enableval" = "xyes" ; then
   if test "x$BUILD_KERNEL" = "x1" -o "x$BUILD_FUSE" = "x1" ; then
      AC_DEFINE(PVFS_USRINT_KMOUNT, 1,
         [Should user interface assume FS is mounted.])
      USRINT_KMOUNT=1
   else
      AC_MSG_ERROR([Assume FS mounted but neither kernel nor FUSE build ])
   fi
else
   AC_DEFINE(PVFS_USRINT_KMOUNT, 0,
      [Should user interface assume FS is mounted.])
fi],
[AC_DEFINE(PVFS_USRINT_KMOUNT, 0,
   [Should user interface assume FS is mounted.])
])

dnl Method to disable user interface CWD support
AC_ARG_ENABLE(usrint-cwd,
[  --disable-usrint-cwd    Do not enable user interface CWD support],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_USRINT_CWD, 1,
      [Should we enable user interface CWD support.])
else
   AC_DEFINE(PVFS_USRINT_CWD, 0,
      [Should we enable user interface CWD support.])
fi],
[if test "x$USRINT_KMOUNT" = "x1" ; then
   AC_DEFINE(PVFS_USRINT_CWD, 0,
      [Should we enable user interface CWD support.])
else
   AC_DEFINE(PVFS_USRINT_CWD, 1,
      [Should we enable user interface CWD support.])
fi])

dnl Method to disable usrint redefining stdin stdout stderr as PVFS streams
AC_ARG_ENABLE(stdio-redefstream,
[  --disable-stdio-redefstream    Do not redefine stdin stdout stderr as PVFS streams],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_STDIO_REDEFSTREAM, 1,
      [Should we redefine stdin stdout stderr as PVFS streams.])
else
   AC_DEFINE(PVFS_STDIO_REDEFSTREAM, 0,
      [Should we redefine stdin stdout stderr as PVFS streams.])
fi],
[AC_DEFINE(PVFS_STDIO_REDEFSTREAM, 1,
      [Should we redefine stdin stdout stderr as PVFS streams.])
])

dnl Method to disable redefining malloc
AC_ARG_ENABLE(malloc-redef,
[  --disable-malloc-redef  Do not redefine malloc],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_MALLOC_REDEF, 1,
      [Should we redefine malloc.])
   MALLOC_REDEF=1
else
   AC_DEFINE(PVFS_MALLOC_REDEF, 0,
      [Should we redefine malloc.])
fi],
[AC_DEFINE(PVFS_MALLOC_REDEF, 1,
      [Should we redefine malloc.])
MALLOC_REDEF=1
])

dnl Method to disable malloc checking a magic number
AC_ARG_ENABLE(malloc-magic,
[  --disable-malloc-magic  Do not have malloc check a magic number],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_MALLOC_MAGIC, 1,
      [Should we have malloc check a magic number.])
else AC_DEFINE(PVFS_MALLOC_MAGIC, 0,
      [Should we have malloc check a magic number.])
fi],
[AC_DEFINE(PVFS_MALLOC_MAGIC, 1,
      [Should we have malloc check a magic number.])
])

dnl Method to disable malloc zeroing new memory
AC_ARG_ENABLE(malloc-zero,
[  --disable-malloc-zero   Do not have malloc zero new memory],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_MALLOC_ZERO, 1,
      [Should we have malloc zero new memory.])
else AC_DEFINE(PVFS_MALLOC_ZERO, 0,
      [Should we have malloc zero new memory.])
fi],
[AC_DEFINE(PVFS_MALLOC_ZERO, 1,
      [Should we have malloc zero new memory.])
])

dnl Method to disable malloc zeroing freed memory
AC_ARG_ENABLE(malloc-free-zero,
[  --disable-malloc-free-zero    Do not have malloc zero freed memory],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_MALLOC_FREE_ZERO, 1,
      [Should we have malloc zero freed memory.])
else AC_DEFINE(PVFS_MALLOC_FREE_ZERO, 0,
      [Should we have malloc zero freed memory.])
fi],
[AC_DEFINE(PVFS_MALLOC_FREE_ZERO, 1,
      [Should we have malloc zero freed memory.])
])

dnl Check for O_CLOEXEC flag in open()
AC_MSG_CHECKING([for O_CLOEXEC flag in open()])
AC_TRY_COMPILE(
    [
     	#include <fcntl.h>
    ], 
    [
    	int flags;
        flags = O_CLOEXEC;
    ],
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_O_CLOEXEC, 1, Define if open() has O_CLOEXEC flag)
    ,
    AC_MSG_RESULT(no)
)

dnl Method to check if dl calls need an explicit link flag
AC_MSG_CHECKING(if dlfns needs explicit library request)
AC_LINK_IFELSE([AC_LANG_SOURCE([
#define _GNU_SOURCE 1
#include <dlfcn.h>
main() {void *p = dlsym(RTLD_DEFAULT,"sym");}
])],[
    AC_MSG_RESULT(no)
],[
    AC_MSG_RESULT(yes)
    LIBS="$LIBS -ldl"
])

dnl Method to enable user environment variable support
BUILD_USER_ENV_VARS=
AC_ARG_ENABLE(user-env-vars,
[  --enable-user-env-vars         Enables usrint environment variable support.],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_USER_ENV_VARS_ENABLED, 1,
      [Should we enable user environment variables.])
   BUILD_USER_ENV_VARS=1
else
   AC_DEFINE(PVFS_USER_ENV_VARS_ENABLED, 0,
      [Should we enable user environment variables.])
fi
AC_SUBST(BUILD_USER_ENV_VARS)],
[AC_DEFINE(PVFS_USER_ENV_VARS_ENABLED, 0,
    [Should we enable user environment variables.])
AC_SUBST(BUILD_USER_ENV_VARS)])

dnl Method to enable user interface data cache
BUILD_UCACHE=
AC_ARG_ENABLE(ucache,
[  --enable-ucache         Enables user interface data cache.],
[if test "x$enableval" = "xyes" ; then
   AC_DEFINE(PVFS_UCACHE_ENABLE, 1,
      [Should we enable user interface data cache.])
   BUILD_UCACHE=1
else
   AC_DEFINE(PVFS_UCACHE_ENABLE, 0,
      [Should we enable user interface data cache.])
fi
AC_SUBST(BUILD_UCACHE)],
[AC_DEFINE(PVFS_UCACHE_ENABLE, 0,
    [Should we enable user interface data cache.])
AC_SUBST(BUILD_UCACHE)])

dnl Method to set JAVA_HOME if it hasn't already been set using --with-jdk
JAVA_HOME=
AC_ARG_WITH(
    jdk,
    [  --with-jdk=<path>       Enable support for JAVA and JNI.],
    [
        dnl Check for jni.h
        AC_CHECK_FILE($withval/include/jni.h,
            [
                AC_DEFINE(HAVE_JNI_H, 1, Define if jni.h exists at the path given with --with-jdk)
                JAVA_HOME=$withval
            ],
            [
                AC_MSG_ERROR(could not locate include/jni.h at the path given with --with-jdk.)
            ]
        )
    ],
    [   with_jdk=no]
)
AC_SUBST(JAVA_HOME)

dnl Method to enable JNI prefixed stdio and posix calls
BUILD_JNI=
AC_ARG_ENABLE(
    jni,
    [  --enable-jni            Enables PVFS Client JNI. Requires --with-jdk.],
    [   if test "x$enableval" = "xyes" ; then
            if test "x$JAVA_HOME" = "x"; then
                AC_MSG_ERROR(--enable-jni must be used in conjunction with --with-jdk=jdk_path.)
            fi

            AC_CHECK_PROG(HAVE_MAVEN, mvn, yes, no)
            if test $HAVE_MAVEN = "no"; then
                AC_MSG_ERROR("maven (mvn) required in PATH to complete JNI client build")
            fi

            AC_DEFINE(PVFS_JNI_ENABLE, 1,
                [Should we enable PVFS Client JNI.])
            BUILD_JNI=1
        else
            AC_DEFINE(PVFS_JNI_ENABLE, 0,
                [Should we enable PVFS Client JNI.])
        fi
    ],
    [   AC_DEFINE(PVFS_JNI_ENABLE, 0,
            [Should we enable PVFS Client JNI.])
    ]
)
AC_SUBST(BUILD_JNI)

dnl See if we have includes needed for acls
BUILD_ACL_INTERFACE=
AC_MSG_CHECKING(for user acl includes sys/acl.h acl/libacl.h)
AC_TRY_COMPILE([
#include <sys/acl.h>
#include <acl/libacl.h>
], [],
    AC_MSG_RESULT(yes)
    BUILD_ACL_INTERFACE=1
    AC_DEFINE(PVFS_HAVE_ACL_INCLUDES, 1,
            [Should we build user interface acl routines.])
,
    AC_MSG_RESULT(no)
    BUILD_ACL_INTERFACE=0
)
AC_SUBST(BUILD_ACL_INTERFACE)

dnl See if scandir compare arg takes void pointers
AC_MSG_CHECKING(for scandir compare arg using void pointers)
AC_TRY_COMPILE([
#define _LARGEFILE64_SOURCE 1
#define _GNU_SOURCE 1
#include <dirent.h>
int scandir (const char *dir, struct dirent ***list,
             int (*sel)(const struct dirent *),
             int (*cmp)(const void *, const void *))
{ return 0; }
int scandir64 (const char *dir, struct dirent64 ***list,
               int (*sel)(const struct dirent64 *),
               int (*cmp)(const void *, const void *))
{ return 0; }
], [],
    AC_MSG_RESULT(yes)
    AC_DEFINE(PVFS_SCANDIR_VOID, 1,
            [scandir compare arg takes void pointers.])
,
    AC_MSG_RESULT(no)
)
    

dnl See if CC is a GNU compiler.  This may require a real test in future
dnl versions of autoconf.  In 2.13 it is a side-effect of AC_PROG_CC.  First
dnl check if it is an Intel compiler; those lie and claim to be gcc but are
dnl not argument compatible
INTELC=
GNUC=
AC_MSG_CHECKING(whether cc is an Intel compiler)
AC_TRY_COMPILE([
#ifndef __ICC
       choke me
#endif
], [],
    AC_MSG_RESULT(yes)
    INTELC=1
    ,
    AC_MSG_RESULT(no)
)
if test "x$INTELC" = "x" ; then
    if test "x$GCC" = "xyes" ; then
       GNUC=1
    fi
fi
AC_SUBST(INTELC)
AC_SUBST(GNUC)

AC_ARG_WITH(efence,
[  --with-efence=<path>    Use electric fence for malloc debugging.],
       if test "x$withval" != "xyes" ; then
              LDFLAGS="${LDFLAGS} -L$withval"
       fi
       AC_CHECK_LIB(efence,malloc,,,-lpthread)
)

dnl Use compiled-in valgrind annotations?  Give the header path if
dnl is not in the usual location.  It is common to install the header
dnl in a subdirectory, so look for that too.  Leave the include path
dnl in CPPFLAGS if it is not a standard header.
AC_ARG_WITH(valgrind,
[  --with-valgrind[=<path>]  Use valgrind annotations for debugging.], [
    found=no
    save_cppflags="$CPPFLAGS"
    AC_MSG_CHECKING(for valgrind.h usability)
    if test "x$withval" = xyes ; then
	AC_COMPILE_IFELSE([AC_LANG_SOURCE([[#include <valgrind.h>]])], found=yes)
	if test x$found = xno ; then
	    CPPFLAGS="$CPPFLAGS -I/usr/include/valgrind"
	    AC_COMPILE_IFELSE([AC_LANG_SOURCE([[#include <valgrind.h>]])], found=yes,
		CPPFLAGS="$save_cppflags")
	fi
    else
	for d in $withval $withval/include \
	         $withval/valgrind $withval/include/valgrind
	do
	    CPPFLAGS="$CPPFLAGS -I$d"
	    AC_COMPILE_IFELSE([AC_LANG_SOURCE([[#include <valgrind.h>]])], found=yes,
		CPPFLAGS="$save_cppflags")
	    if test x$found = xyes ; then
		break
	    fi
	done
    fi
    AC_MSG_RESULT($found)
    if test x$found = xyes ; then
	AC_DEFINE(HAVE_VALGRIND_H, 1, Define if include file valgrind.h exists)
    fi
])

if test "x$NEED_BERKELEY_DB" = "xyes" ; then
    dnl
    dnl make sure that required db development package is installed
    dnl and valid; path can be specified at configure time
    dnl
    AC_ARG_WITH(db,
           [  --with-db=<dir>         Location of installed DB package (default=/usr)],
    	   [AX_BERKELEY_DB(${withval})],
	   [AX_BERKELEY_DB("")])
fi
dnl end of NEED_BERKELEY_DB section

if test "x$BUILD_SERVER" = "x1"; then
	dnl check if -lrt is required
	AX_CHECK_NEEDS_LIBRT
fi

case "$host_os" in

    *darwin*)
	AC_DEFINE([TARGET_OS_DARWIN], 1, Define if on darwin)
    	TARGET_OS_DARWIN=1
        CFLAGS="$CFLAGS -D__DARWIN__"
	AC_SUBST(TARGET_OS_DARWIN)
        AC_SUBST(CFLAGS)
    ;;
    *linux*)
	AC_DEFINE([TARGET_OS_LINUX], 1, Define if on linux)
    	TARGET_OS_LINUX=1
	AC_SUBST(TARGET_OS_LINUX)
    ;;
esac

dnl check for gethostbyname function
AC_MSG_CHECKING(for gethostbyname)
oldcflags="$CFLAGS"
CFLAGS="$USR_CFLAGS"
AC_TRY_COMPILE([
#include <netdb.h>
], [
gethostbyname("localhost");
],
AC_MSG_RESULT(yes)
AC_DEFINE(HAVE_GETHOSTBYNAME, 1, Define if gethostbyname function exists),
AC_MSG_RESULT(no)
WARN_ABOUT_HOSTNAMES="yes")

AC_MSG_CHECKING(for gethostbyaddr)
AC_TRY_COMPILE([
#include <netdb.h>
], [
struct sockaddr_in peer;
gethostbyaddr((void *)&peer.sin_addr.s_addr, sizeof(struct in_addr), AF_INET);
],
AC_MSG_RESULT(yes)
AC_DEFINE(HAVE_GETHOSTBYADDR, 1, Define if gethostbyaddr function exists),
AC_MSG_RESULT(no)  )

CFLAGS="$oldcflags"

dnl allow disabling TCP BMI method
BUILD_BMI_TCP=1
AC_ARG_WITH(bmi-tcp,
[  --without-bmi-tcp       Disables BMI TCP method],
    if test -z "$withval" -o "$withval" = yes ; then
	:
    elif test "$withval" = no ; then
	BUILD_BMI_TCP=
    else
	AC_MSG_ERROR([Option --with-tcp requires yes/no argument.])
    fi
)
AC_SUBST(BUILD_BMI_TCP)

dnl
dnl Configure bmi_gm, if --with-gm or a variant given.
dnl
AX_GM

dnl
dnl Configure bmi_mx, if --with-mx or a variant given.
dnl
AX_MX

dnl
dnl Configure bmi_ib, if --with-ib or a variant given.
dnl
AX_IB

dnl
dnl Configure bmi_portals, if --with-portals or a variant given.
dnl
AX_PORTALS

dnl
dnl Configure bmi_zoid, if --with-zoid or a variant given.
dnl
AX_ZOID

dnl
dnl check for F_NOCACHE (MACOSX)
AC_MSG_CHECKING([for F_NOCACHE])
AC_TRY_COMPILE([
    #include <sys/fcntl.h>
    int fd;
    fcntl(fd, F_NOCACHE, 1);
    ], [],
    AC_MSG_RESULT(no),
    AC_MSG_RESULT(yes),
    AC_DEFINE(HAVE_FCNTL_F_NOCACHE, 1, Define if system defines F_NOCACHE fcntl)
)

dnl
dnl check for open O_DIRECT
AC_MSG_CHECKING([for open O_DIRECT])
AC_TRY_COMPILE([
    #define _GNU_SOURCE
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    ], [
    open("somefile", O_RDONLY | O_DIRECT);
    ],
    AC_MSG_RESULT(yes)
    AC_DEFINE(HAVE_OPEN_O_DIRECT, 1, Define if system defines O_DIRECT fcntl),
    AC_MSG_RESULT(no)
)

dnl
dnl check if fgetxattr takes extra arguments
AC_MSG_CHECKING([for fgetxattr extra arguments])
AC_TRY_COMPILE([
	#include <sys/types.h>
	#ifdef HAVE_ATTR_XATTR_H
	#include <attr/xattr.h>
	#endif
	#ifdef HAVE_SYS_XATTR_H
	#include <sys/xattr.h>
	#endif
	],
	[
	fgetxattr(0, 0, 0, 0, 0, 0);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FGETXATTR_EXTRA_ARGS, 1, Define if fgetxattr takes position and option arguments),
	AC_MSG_RESULT(no)
)

dnl
dnl check if setxattr takes extra arguments
AC_MSG_CHECKING([for setxattr extra arguments])
AC_TRY_COMPILE([
	#include <sys/types.h>
	#ifdef HAVE_ATTR_XATTR_H
	#include <attr/xattr.h>
	#endif
	#ifdef HAVE_SYS_XATTR_H
	#include <sys/xattr.h>
	#endif
	],
	[
	setxattr(0, 0, 0, 0, 0, 0);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_SETXATTR_EXTRA_ARGS, 1, Define if setxattr takes position and option arguments),
	AC_MSG_RESULT(no)
)

dnl
dnl check if getxattr takes extra arguments
AC_MSG_CHECKING([for getxattr extra arguments])
AC_TRY_COMPILE([
	#include <sys/types.h>
	#ifdef HAVE_ATTR_XATTR_H
	#include <attr/xattr.h>
	#endif
	#ifdef HAVE_SYS_XATTR_H
	#include <sys/xattr.h>
	#endif
	],
	[
	getxattr(0, 0, 0, 0, 0, 0);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_GETXATTR_EXTRA_ARGS, 1, Define if getxattr takes position and option arguments),
	AC_MSG_RESULT(no)
)

dnl
dnl look for sysinfo header
AC_CHECK_HEADER(sys/sysinfo.h,
       AC_DEFINE(HAVE_SYSINFO, 1, Define if sysinfo.h is present)
       )

dnl for regular functions, add another AC_CHECK_FUNCS line
AC_CHECK_FUNCS(strnlen)
AC_CHECK_FUNCS(strtoull)
AC_CHECK_FUNCS(strstr)
AC_CHECK_FUNCS(fgetxattr)
AC_CHECK_FUNCS(fsetxattr)

dnl fgetxattr doesn't have a prototype on some systems
AC_MSG_CHECKING([for fgetxattr prototype])
AC_TRY_COMPILE([
	#include <sys/types.h>
	#ifdef HAVE_ATTR_XATTR_H
	#include <attr/xattr.h>
	#endif
	#ifdef HAVE_SYS_XATTR_H
	#include <sys/xattr.h>
	#endif
	char * fgetxattr(char *foo);
	], [], 
	AC_MSG_RESULT(no),
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FGETXATTR_PROTOTYPE, 1, Define if system provides fgtxattr prototype)
)

dnl
dnl check if fgetxattr takes extra arguments:  old systems that do not have any
dnl sort of fgetxattr should fall back to our faked version.

AC_MSG_CHECKING([for fgetxattr extra arguments])
AC_TRY_COMPILE([
	#include <sys/types.h>
	#ifdef HAVE_ATTR_XATTR_H
	#include <attr/xattr.h>
	#endif
	#ifdef HAVE_SYS_XATTR_H
	#include <sys/xattr.h>
	#endif
	],
	[
	#ifdef HAVE_FGETXATTR_PROTOTYPE
	fgetxattr(0, 0, 0, 0, 0, 0);
	#else
	#  error
	#endif
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FGETXATTR_EXTRA_ARGS, 1, Define if fgetxattr takes position and option arguments),
	AC_MSG_RESULT(no)
)

AC_MSG_CHECKING([for fsetxattr extra arguments])
AC_TRY_COMPILE([
	#include <sys/types.h>
	#ifdef HAVE_ATTR_XATTR_H
	#include <attr/xattr.h>
	#endif
	#ifdef HAVE_SYS_XATTR_H
	#include <sys/xattr.h>
	#endif
	],
	[
	fsetxattr(0, 0, 0, 0, 0, 0);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FSETXATTR_EXTRA_ARGS, 1, Define if fsetxattr takes position and option arguments),
	AC_MSG_RESULT(no)
)


dnl check for getmntent(), special test
AC_FUNC_GETMNTENT


AC_ARG_ENABLE(static-server,
[  --enable-static-server  Builds the server static],
[ staticserver=$enableval ],
[ staticserver="no" ] )

if test "$staticserver" = "yes"; then
	SERVER_LDFLAGS="$LDFLAGS -static"
else
	SERVER_LDFLAGS="$LDFLAGS -rdynamic"
fi

AC_SUBST(SERVER_LDFLAGS)

dnl this is a special case, used to key off options controlling gossip
dnl   backtrace capability
AC_CHECK_HEADERS(execinfo.h,
	LDFLAGS="$LDFLAGS -rdynamic"
	GOSSIP_ENABLE_BACKTRACE=1
)
AC_SUBST(GOSSIP_ENABLE_BACKTRACE)

dnl --
dnl Check for SDL
test_for_sdl()
{
  AC_MSG_CHECKING([for SDL])
  SDL_CONFIG=`which sdl-config 2> /dev/null`
  if ! test -z "$SDL_CONFIG" && test -x "$SDL_CONFIG"; then
     AC_MSG_RESULT(yes)
     SDL_VERSION=`sdl-config --version`
     VISCFLAGS=`sdl-config --cflags`
     VISCLIBS=`sdl-config --libs`

     dnl We also need SDL ttf support
     dnl only define BUILD_VIS if we have all required parts
     AC_CHECK_HEADERS(SDL_ttf.h,
         [BUILD_VIS="1"
          CPPFLAGS="$CPPFLAGS $VISCFLAGS"],
          [AC_CHECK_HEADERS(SDL/SDL_ttf.h,
              [BUILD_VIS="1"
              CPPFLAGS="$CPPFLAGS $VISCFLAGS"],)],
  )

     AC_SUBST(BUILD_VIS)
     AC_SUBST(VISCFLAGS)
     AC_SUBST(VISLIBS)
  else
     AC_MSG_RESULT(no)
  fi
}

dnl optionally disable building visualisation tools
AC_ARG_ENABLE(visual,
[  --enable-visual         Enables visualisation tools. (Disabled by default, requires SDL)],
[
if test "x$enableval" = "xyes" ; then
   test_for_sdl
fi
],
)

dnl in our src/io/bmi/bmi-byteswap.h we check to see if __bswap_64
dnl is defined, and if not, we define it. We probably haven't
dnl had to use our define for a long time, __bswap_64 has been
dnl defined in some glibc package (wherever /usr/include/bits/byteswap.h
dnl comes from) for a long time. We could probably get rid of our
dnl check in bmi-byteswap.h: even as far back as Red Hat 7.3 where
dnl /usr/include/bits/byteswap.h came from glibc-devel-2.2.5.34, __bswap_64
dnl would be defined for anyone using gcc > 2.0.
dnl
dnl Anyhow, more recently (for example: when /usr/include/bits/byteswap.h
dnl comes from glibc-headers-2.17-11.fc19.x86_64), dnl __bswap_64 is
dnl an inline function in most situations. This causes our define of
dnl __bswap_64 to kick in (since __bswap_64 is NOT defined) and this
dnl is a problem - our stinky old define overrides the inline function.
dnl This test sets a define if __bswap_64 is a function. We can use
dnl this define in src/io/bmi/bmi-byteswap.h to avoid defining __bswap_64
dnl if it is an inline function. This way, we can leave the test in 
dnl src/io/bmi/bmi-byteswap.h in case there's anyone out there who
dnl still needs it.
AC_MSG_CHECKING([for __bswap_64 is a function])
AC_TRY_COMPILE(
    [
      #include <byteswap.h>
      #include <bits/types.h>
    ],
    [
      __uint64_t x;
      #ifndef  __bswap_64
        __bswap_64(x);
      #else
         __bswap_64_is_not_a_function(not a function);
      #endif
    ],
    [
      AC_MSG_RESULT(yes)
      AC_DEFINE(BSWAP_64_IS_A_FUNC, 1, Define if __bswap_64 is a function)
    ]
    ,
    AC_MSG_RESULT(no)
)

dnl gets has been officially removed in ISO C11
dnl     - /usr/include/stdio.h
dnl     - glibc-headers-2.17-11.fc19.x86_64
dnl
dnl glibc 2.15 and later don't declare gets anymore for ISO C11 mode 
dnl and if _GNU_SOURCE is defined.
dnl     - http://gcc.gnu.org/bugzilla/show_bug.cgi?id=51785
AC_MSG_CHECKING([for gets in stdio.h])
oldcflags="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_TRY_COMPILE(
    [
       #include <stdio.h>
    ],
    [
       char *s;
       gets(s);
    ],
    [
       AC_MSG_RESULT(yes)
       AC_DEFINE(HAVE_STDIO_GETS, 1, Define if gets supported in stdio.h)
    ],
    [
       AC_MSG_RESULT(no)
    ]
)
CFLAGS="$oldcflags"

dnl create any missing subdirectories that wouldn't be created
dnl by AC_OUTPUT below (which won't create intermediate dirs).
for d in src/apps src/io src/common src/client src/kernel \
       doc src/apps/kernel test; do
       install -d $d;
done

dnl output final version of top level makefile and subdirectory
dnl    makefile includes
AC_OUTPUT(include/pvfs2.h
Makefile
module.mk
src/apps/admin/module.mk
src/apps/admin/pvfs2-config
src/apps/devel/module.mk
src/apps/karma/module.mk
src/apps/vis/module.mk
src/apps/fuse/module.mk
src/apps/ucache/module.mk
src/apps/kernel/linux/module.mk
src/apps/user/module.mk
src/io/trove/module.mk
src/io/trove/trove-handle-mgmt/module.mk
src/io/trove/trove-dbpf/module.mk
src/common/misc/module.mk
src/common/quickhash/module.mk
src/common/quicklist/module.mk
src/common/dotconf/module.mk
src/common/id-generator/module.mk
src/common/gossip/module.mk
src/common/gen-locks/module.mk
src/common/llist/module.mk
src/common/statecomp/module.mk
src/common/events/module.mk
src/common/mgmt/module.mk
src/common/security/module.mk
src/common/hash/module.mk
src/common/token-utils/module.mk
src/io/bmi/module.mk
src/io/bmi/bmi_tcp/module.mk
src/io/bmi/bmi_gm/module.mk
src/io/bmi/bmi_mx/module.mk
src/io/bmi/bmi_ib/module.mk
src/io/bmi/bmi_portals/module.mk
src/io/bmi/bmi_zoid/module.mk
src/io/description/module.mk
src/io/flow/module.mk
src/io/flow/flowproto-bmi-trove/module.mk
src/io/flow/flowproto-template/module.mk
src/io/flow/flowproto-dump-offsets/module.mk
src/io/flow/flowproto-bmi-cache/module.mk
src/io/buffer/module.mk
src/io/job/module.mk
src/io/dev/module.mk
src/proto/module.mk
src/server/module.mk
src/server/request-scheduler/module.mk
src/client/jni/module.mk
src/client/sysint/module.mk
src/client/usrint/module.mk
src/kernel/linux-2.6/Makefile
src/kernel/linux-2.4/Makefile
doc/module.mk
doc/coding/module.mk
doc/design/module.mk
doc/random/module.mk
examples/orangefs-server
examples/orangefs-client
doc/doxygen/pvfs2-doxygen.conf
src/client/jni/pom.xml
src/client/hadoop/orangefs-hadoop1/pom.xml
src/client/hadoop/orangefs-hadoop1/src/main/resources/conf/hadoop-env.sh
src/client/hadoop/orangefs-hadoop2/pom.xml
src/client/hadoop/orangefs-hadoop2/src/main/resources/conf/hadoop-env.sh
)

chmod +x $BUILD_ABSOLUTE_TOP/src/apps/admin/pvfs2-config

# print a summary of the configuration information
if test "x$BUILD_BMI_ONLY" = "x1" ; then
	AC_MSG_RESULT()
	AC_MSG_RESULT([***** Displaying BMI configuration information *****])
	AC_MSG_RESULT([----------------------------------------------------])
	PKGSTR="BMI"
else
	AC_MSG_RESULT()
	AC_MSG_RESULT([***** Displaying PVFS Configuration Information *****])
	AC_MSG_RESULT([------------------------------------------------------])
	PKGSTR="PVFS2"
fi

# print PVFS configs (not used by BMI)
if test "x$BUILD_BMI_ONLY" != "x1" ; then

if test "x$BUILD_KARMA" = "x1" ; then
   AC_MSG_RESULT([PVFS2 configured to build karma gui               : yes])
else
   AC_MSG_RESULT([PVFS2 configured to build karma gui               :  no])
fi

if test "x$BUILD_VIS" = "x1" ; then
   AC_MSG_RESULT([PVFS2 configured to build visualization tools     : yes])
else
   AC_MSG_RESULT([PVFS2 configured to build visualization tools     :  no])
fi

if test "x$ENABLE_COVERAGE" = "x1" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 configured to perform coverage analysis     : yes])
else
   AC_MSG_RESULT([PVFS2 configured to perform coverage analysis     :  no])
fi

if test "x$MISC_TROVE_FLAGS" = "x" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 configured for aio threaded callbacks       :  no])
else
   AC_MSG_RESULT([PVFS2 configured for aio threaded callbacks       : yes])
fi

if test "x$BUILD_FUSE" = "x1" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 configured to use FUSE                      : yes])
else
   AC_MSG_RESULT([PVFS2 configured to use FUSE                      :  no])
fi

if test "x$LINUX_KERNEL_SRC" = "x" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 configured for the 2.6/3 kernel module      :  no])
else
   AC_MSG_RESULT([PVFS2 configured for the 2.6/3 kernel module      : yes])
fi

if test "x$LINUX24_KERNEL_SRC" = "x" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 configured for the 2.4.x kernel module      :  no])
else
   AC_MSG_RESULT([PVFS2 configured for the 2.4.x kernel module      : yes])
fi

if test "x$MMAP_RA_CACHE" = "x" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 configured for using the mmap-ra-cache      :  no])
else
   AC_MSG_RESULT([PVFS2 configured for using the mmap-ra-cache      : yes])
fi

if test "x$RESET_FILE_POS" = "x" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 configured for resetting file position      :  no])
else
   AC_MSG_RESULT([PVFS2 configured for resetting file position      : yes])
fi

if test "x$REDHAT_RELEASE" = "x" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 will use workaround for redhat 2.4 kernels  :  no])
else
   AC_MSG_RESULT([PVFS2 will use workaround for redhat 2.4 kernels  : yes])
fi

if test "x$NPTL_WORKAROUND" = "x" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 will use workaround for buggy NPTL          :  no])
else
   AC_MSG_RESULT([PVFS2 will use workaround for buggy NPTL          : yes])
fi

if test "x$ENABLE_CAPCACHE" = "x1"; then
   AC_MSG_RESULT([PVFS2 server capability cache enabled             : yes])
else
   AC_MSG_RESULT([PVFS2 server capability cache enabled             :  no])
fi

if test "x$ENABLE_CREDCACHE" = "x1"; then
   AC_MSG_RESULT([PVFS2 server credential cache enabled             : yes])
else
   AC_MSG_RESULT([PVFS2 server credential cache enabled             :  no])
fi

if test "x$ENABLE_CERTCACHE" = "x1"; then
   AC_MSG_RESULT([PVFS2 server certificate cache enabled            : yes])
else
   AC_MSG_RESULT([PVFS2 server certificate cache enabled            :  no])
fi

if test "x$ENABLE_SECURITY_KEY" = "x1"; then
   AC_MSG_RESULT([PVFS2 configured with key-based security          : yes])
else
   AC_MSG_RESULT([PVFS2 configured with key-based security          :  no])
fi

if test "x$ENABLE_SECURITY_CERT" = "x1"; then
   AC_MSG_RESULT([PVFS2 configured with certificate-based security  : yes])
else
   AC_MSG_RESULT([PVFS2 configured with certificate-based security  :  no])
fi

if test "x$BUILD_SERVER" = "x1" -a "x$BUILD_BMI_ONLY" != "x1"; then
   AC_MSG_RESULT([PVFS2 server will be built                        : yes])
else
   AC_MSG_RESULT([PVFS2 server will be built                        :  no])
fi

if test "x$ENABLE_PROFILING" = "x1"; then
   AC_MSG_RESULT([PVFS2 configured with profiling enabled           : yes])
else
   AC_MSG_RESULT([PVFS2 configured with profiling enabled           :  no])
fi

if test "x$BUILD_USRINT" = "x1" ; then
   AC_MSG_RESULT([PVFS2 user interface libraries will be built      : yes])
else
   AC_MSG_RESULT([PVFS2 user interface libraries will be built      :  no])
fi

if test "x$BUILD_SYMBOLIC" = "x1" ; then
   AC_MSG_RESULT([PVFS2 symbolic libraries will be built            : yes])
else
   AC_MSG_RESULT([PVFS2 symbolic libraries will be built            :  no])
fi

if test "x$BUILD_USER_ENV_VARS" = "x1" ; then
   AC_MSG_RESULT([PVFS2 user environment variables enabled          : yes])
else
   AC_MSG_RESULT([PVFS2 user environment variables enabled          :  no])
fi

if test "x$BUILD_UCACHE" = "x1" ; then
   AC_MSG_RESULT([PVFS2 user interface library cache enabled        : yes])
else
   AC_MSG_RESULT([PVFS2 user interface library cache enabled        :  no])
fi

if test "x$MALLOC_REDEF" = "x1" ; then
   AC_MSG_RESULT([Malloc will be redefined                          : yes])
else
   AC_MSG_RESULT([Malloc will be redefined                          :  no])
fi

if test "x$BUILD_JNI" = "x1" ; then
   AC_MSG_RESULT([PVFS2 client JNI enabled                          : yes])
else
   AC_MSG_RESULT([PVFS2 client JNI enabled                          :  no])
fi

else

# print BMI and PVFS configs
if test "x$THREAD_LIB" = "x"; then
   AC_MSG_RESULT([$PKGSTR configured for a thread-safe client library :  no])
else
   AC_MSG_RESULT([$PKGSTR configured for a thread-safe client library : yes])
fi

if test "x$BUILD_EPOLL" = "x1" ; then
   AC_MSG_RESULT([$PKGSTR configured to use epoll                     : yes])
else
   AC_MSG_RESULT([$PKGSTR configured to use epoll                     :  no])
fi

if test "x$TRUSTED_CONNECTIONS" = "x" ; then
	AC_MSG_RESULT([$PKGSTR configured for using trusted connections    :  no])
else
	AC_MSG_RESULT([$PKGSTR configured for using trusted connections    : yes])
fi

fi # end of BMI/PVFS config display

if test "x$WARN_ABOUT_HOSTNAMES" = "xyes" ; then
   AC_MSG_RESULT(WARNING: gethostbyname is not supported on this machine: ALL ADDRESSES MUST BE IN DOT NOTATION.)
fi


dnl
dnl Warn about potential slowness if using a "fast" network along with
dnl TCP.  Polling with timeout in BMI is implemented by sequentially asking
dnl each device to poll for half of the timeout.  This leads to a fixed 5 ms
dnl delay in the TCP device before the IB device can be queried, for instance.
dnl When traffic will only appear on one device, suggest that TCP be disabled.
dnl But leave TCP as default on, certainly, as that is what most people want.
dnl
if test x$BUILD_GM = x1 -o x$BUILD_MX = x1 -o x$BUILD_IB = x1 -o \
	x$BUILD_OPENIB = x1 -o x$BUILD_PORTALS = x1 -o x$BUILD_ZOID = x1 ; then
    if test x$BUILD_BMI_TCP = x1 ; then
	AC_MSG_WARN([You have selected to build $PKGSTR to use a "fast" network
		    interface, but have not disabled TCP.  The way this is
		    currently implemented will lead to rather slow response
		    times on the fast interface.  Suggest you configure with
		    "--without-bmi-tcp" for the best performance.])
    fi
fi

if test x$HAVE_DB_OLD = x1; then
    AC_MSG_WARN([The detected version of Berkeley DB is not at least 4.8.30.
                    Metadata corruption has been documented in versions prior 
                    to 4.8.30. It is highly recommended that you update the 
                    version of Berkeley DB you are building against.])
fi

AC_MSG_RESULT()
AC_MSG_RESULT([$PKGSTR version string: $PVFS2_VERSION])

AC_MSG_RESULT()
