AC_PREREQ([2.63])
AC_INIT([orangefs-webpack], [2.9], [hubcap@clemson.edu])
AC_CONFIG_SRCDIR([d.dav/mod_dav_orangefs.c])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([Makefile
                 d.admin/Makefile
                 d.authn/Makefile
                 d.dav/Makefile
                 d.s3/Makefile])

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE
AC_PROG_LIBTOOL

AC_ARG_ENABLE([admin],
              AS_HELP_STRING([--enable-admin],
                             [Enables the mod_orangefs_admin module.]),
              [admin=${enableval}], [admin=no])
AC_ARG_ENABLE([authn],
              AS_HELP_STRING([--enable-authn],
                             [Enables the mod_authn_orangefs module.]),
              [authn=${enableval}], [authn=no])
AC_ARG_ENABLE([dav],
              AS_HELP_STRING([--enable-dav],
                             [Enables the mod_dav_orangefs module.]),
              [dav=${enableval}], [dav=no])
AC_ARG_ENABLE([s3],
              AS_HELP_STRING([--enable-s3],
                             [Enables the mod_orangefs_s3 module.]),
              [s3=${enableval}], [s3=no])

AC_ARG_WITH([apxs],
            AS_HELP_STRING([--with-apxs=/path/to/apxs],
                           [The path to the installed apxs executable.]))
AC_ARG_WITH([pvfs2-config],
            AS_HELP_STRING([--with-pvfs2-config=/path/to/pvfs2-config],
                           [The path to the installed pvfs2-config executable.]))
AC_ARG_WITH([pvfs2-source],
            AS_HELP_STRING([--with-pvfs2-source=/path/to/pvfs2-source],
                           [The path to the pvfs2 source. This is required because it contains some important header files.]))
AC_ARG_WITH([xml2_config],
            AS_HELP_STRING([--with-xml2-config=/path/to/xml2-config],
                           [The path to the installed xml2-config executable.]))

if test "x$with_apxs" != "x"; then
	WP_APXS="$with_apxs"
elif test -f /usr/bin/apxs; then
	WP_APXS=/usr/bin/apxs
elif test -f /usr/sbin/apxs; then
	WP_APXS=/usr/sbin/apxs
elif test -f /usr/local/bin/apxs; then
	WP_APXS=/usr/local/bin/apxs
elif test -f /usr/local/sbin/apxs; then
	WP_APXS=/usr/local/sbin/apxs
else
	AC_MSG_ERROR([*** Use --with-apxs=/path/to/apxs])
fi

if test "x$with_pvfs2_config" != "x"; then
	WP_PVFS2_CONFIG="$with_pvfs2_config"
elif test -f /usr/bin/pvfs2-config; then
	WP_PVFS2_CONFIG=/usr/bin/pvfs2-config
elif test -f /usr/sbin/pvfs2-config; then
	WP_PVFS2_CONFIG=/usr/sbin/pvfs2-config
elif test -f /usr/local/bin/pvfs2-config; then
	WP_PVFS2_CONFIG=/usr/local/bin/pvfs2-config
elif test -f /usr/local/sbin/pvfs2-config; then
	WP_PVFS2_CONFIG=/usr/local/isbin/pvfs2-config
else
	AC_MSG_ERROR([*** Use --with-pvfs2-config=/path/to/pvfs2-config])
fi

if test \( "x$with_pvfs2_source" = "x" \) -a \( "$admin" = yes \); then
	AC_MSG_ERROR([*** Use --with-pvfs2-source=/path/to/pvfs2-config])
else
	WP_PVFS2_SOURCE=$with_pvfs2_source
fi

WP_XML2_CONFIG=
if test "x$with_xml2_config" != "x"; then
	WP_XML2_CONFIG="$with_xml2_config"
elif test -f /usr/bin/xml2-config; then
	WP_XML2_CONFIG=/usr/bin/xml2-config
elif test -f /usr/sbin/xml2-config; then
	WP_XML2_CONFIG=/usr/sbin/xml2-config
elif test -f /usr/local/bin/xml2-config; then
	WP_XML2_CONFIG=/usr/local/bin/xml2-config
elif test -f /usr/local/sbin/xml2-config; then
	WP_XML2_CONFIG=/usr/local/sbin/xml2-config
elif test "s3" = yes; then
	AC_MSG_ERROR([*** Use --with-xml2-config=/path/to/xml2-config])
fi

WP_SUBDIRS=
if test "$admin" = yes; then
	WP_SUBDIRS+=d.admin' '
fi
if test "$authn" = yes; then
	WP_SUBDIRS+=d.authn' '
fi
if test "$dav" = yes; then
	WP_SUBDIRS+=d.dav' '
fi
if test "$s3" = yes; then
	WP_SUBDIRS+=d.s3' '
fi
WP_SUBDIRS=${WP_SUBDIRS% }

AC_SUBST(WP_APXS)
AC_SUBST(WP_PVFS2_CONFIG)
AC_SUBST(WP_PVFS2_SOURCE)
AC_SUBST(WP_XML2_CONFIG)
AC_SUBST(WP_SUBDIRS)
AC_OUTPUT
