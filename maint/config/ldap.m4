# ----------------------------------------------------------
#    Sam Sampson <sampson@omnibond.com>     Nov-19-2012
#  based on work by:
#    Eugene Grigorjev <eugene@zabbix.com>   Feb-02-2007
#
# Users may override the detected values by doing something like:
# LDAP_LDFLAGS="-lldap" LDAP_CPPFLAGS="-I/usr/myinclude" ./configure
#
# --with-ldap={directory}
#
# AX_LDAP()
#
# configure.in
#   :AC_SUBST(LDAP_CPPFLAGS)
#   :AC_SUBST(LDAP_LDFLAGS)
#   :AC_SUBST(LDAP_LIB)
#
# config.h.in
#   :HAVE_LDAP
#
# This macro is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


AC_DEFUN([AX_LDAP],
[
    AC_ARG_WITH([ldap],
                AC_HELP_STRING([--with-ldap=DIR],
                               [Location of OpenLDAP installation (for certificate-based security)]),
                [ if test "$withval" = "no"; then
                      want_ldap="no"
                      _libldap_with="no"
                  elif test "$withval" = "yes"; then
                      want_ldap="yes"
                      _libldap_with="yes"
                  else
                      want_ldap="yes"
                      _libldap_with=$withval
                  fi
                ],
                [ if test "x$ENABLE_SECURITY_CERT" = "x1"; then
                      want_ldap="yes"
                      _libldap_with="yes"
                  else
                      want_ldap="no"
                      _libldap_with="no"
                  fi
                ])

    if test "x$_libldap_with" != x"no"; then
        AC_MSG_CHECKING(for LDAP support)

        if test "$_libldap_with" = "yes"; then
            if test -f /usr/local/openldap/include/ldap.h; then
                LDAP_INCDIR=/usr/local/openldap/include/
                LDAP_LIBDIR=/usr/local/openldap/lib/
                found_ldap="yes"
            elif test -f /usr/include/ldap.h; then
                LDAP_INCDIR=/usr/include
                if test -d /usr/lib64; then
                    LDAP_LIBDIR=/usr/lib64
                else
                    LDAP_LIBDIR=/usr/lib
                fi
                found_ldap="yes"
            elif test -f /usr/local/include/ldap.h; then
                LDAP_INCDIR=/usr/local/include
                if test -d /usr/local/lib64; then
                    LDAP_LIBDIR=/usr/local/lib64
                else
                    LDAP_LIBDIR=/usr/local/lib
                fi
                found_ldap="yes"
            else
                found_ldap="no"
                AC_MSG_RESULT(no)
            fi
        else
            if test -f $_libldap_with/include/ldap.h; then
                LDAP_INCDIR=$_libldap_with/include
                LDAP_LIBDIR=$_libldap_with/lib
                found_ldap="yes"
            else
                found_ldap="no"
                AC_MSG_RESULT(no)
            fi
        fi

       if test "x$found_ldap" != "xno" ; then
           if test "$_libldap_with" != "yes"; then
                LDAP_CPPFLAGS="I$LDAP_INCDIR"
                LDAP_LDFLAGS="-L$LDAP_LIBDIR"
           fi

           found_ldap="yes"           
# set in src/common/security/module.mk.in:
#          AC_DEFINE(LDAP_DEPRECATED, 1, [Define to 1 if LDAP deprecated functions are used.])
           AC_MSG_RESULT(yes)
           
           # avoid adding libraries to LIBS w/dummy line
           AC_CHECK_LIB(lber, ber_init, [_dummy=x], 
                        AC_MSG_ERROR([Could not link against liblber.so]))
           AC_CHECK_LIB(ldap, ldap_init, [_dummy=x], 
                        AC_MSG_ERROR([Could not link against libldap.so]),
                        [-llber])

           LDAP_LIB="-lldap -llber"
       fi
  fi

  AC_SUBST(LDAP_CPPFLAGS)
  AC_SUBST(LDAP_LDFLAGS)
  AC_SUBST(LDAP_LIB)

  unset _libldap_with
  unset found_ldap
  unset _dummy
])dnl
