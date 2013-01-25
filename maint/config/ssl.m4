#*-
# Copyright (c) 2008 Masashi Osakabe
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $Id: ssl.m4,v 1.1.4.1 2011-11-04 20:40:24 sampson Exp $
#
#
# --with-ssl={directory}
#
# AX_OPENSSL()
#
# configure.in
#   :AC_SUBST(OPENSSL_CPPFLAGS)
#   :AC_SUBST(OPENSSL_LDFLAGS)
#   :AC_SUBST(OPENSSL_LIB)
#
# config.h.in
#   :HAVE_OPENSSL
#

AC_DEFUN([AX_OPENSSL],
[
    AC_ARG_WITH([ssl],
                AS_HELP_STRING([--with-ssl=DIR],
                [Location of the OpenSSL installation (if custom)]),
                [
                if test "$withval" = "no"; then
                    want_ssl="no"
                elif test "$withval" = "yes"; then
                    want_ssl="yes"
                    ac_ssl_path=""
                else
                    want_ssl="yes"
                    ac_ssl_path="$withval"
                fi
                ],
                [want_ssl="yes"])

    if test "x$want_ssl" = "xyes"; then
        AC_REQUIRE([AC_PROG_CC])
        AC_MSG_CHECKING(for openssl/md5.h)

        if test "$ac_ssl_path" != ""; then
            OPENSSL_CPPFLAGS="-I$ac_ssl_path/include"
            if test -d "$ac_ssl_path/lib64"; then
                OPENSSL_LDFLAGS="-L$ac_ssl_path/lib64"
            fi
            OPENSSL_LDFLAGS="$OPENSSL_LDFLAGS -L$ac_ssl_path/lib"
        else
            for ac_ssl_path_tmp in /usr /usr/local /opt ; do
                if test -d "$ac_ssl_path_tmp" && test -r "$ac_ssl_path_tmp/include/openssl/md5.h"; then
                    OPENSSL_CPPFLAGS="-I$ac_ssl_path_tmp/include"
                    if test -d "$ac_ssl_path_tmp/lib64"; then
                        OPENSSL_LDFLAGS="-L$ac_ssl_path_tmp/lib64"
                    fi
                    OPENSSL_LDFLAGS="$OPENSSL_LDFLAGS -L$ac_ssl_path_tmp/lib"
                    break;
                fi
            done
        fi

        if test "$OPENSSL_CPPFLAGS" = "";then
            AC_MSG_RESULT(no)
            AC_MSG_WARN('*** openssl/md5.h does not exist')
        else
            AC_MSG_RESULT(yes)

            CPPFLAGS="$CPPFLAGS $OPENSSL_CPPFLAGS"
            export CPPFLAGS
            LDFLAGS="$LDFLAGS $OPENSSL_LDFLAGS"
            export LDFLAGS

            AC_SUBST(OPENSSL_CPPFLAGS)
            AC_SUBST(OPENSSL_LDFLAGS)

            ax_lib=ssl
            AC_CHECK_LIB($ax_lib, SSLv23_method,
                [link_ssl="yes";break], [link_ssl="no"])
            if test "x$link_ssl" = "xno"; then
                AC_MSG_WARN(Could not link against lib$ax_lib !)
            else
                AC_DEFINE(HAVE_OPENSSL, ,
                    [Define to 1 if OpenSSL is available])
            fi

            ax_lib=crypto
            AC_CHECK_LIB($ax_lib, MD5_Init,
                [link_crypto="yes";break], [link_crypto="no"])
            if test "x$link_crypto" = "xno"; then
                AC_MSG_WARN(Could not link against lib$ax_lib !)
            else
                if test "x$link_ssl" = "xno"; then
                    AC_DEFINE(HAVE_OPENSSL, ,
                        [Define to 1 if OpenSSL is available])
                fi
            fi

            if test "x$link_ssl" = "xyes"; then
                OPENSSL_LIB="$OPENSSL_LIB -lssl"
            fi
            if test "x$link_crypto" = "xyes"; then
                OPENSSL_LIB="$OPENSSL_LIB -lcrypto"
            fi
            AC_SUBST(OPENSSL_LIB)

            if test -d "/usr/kerberos/include" ; then
                OPENSSL_CPPFLAGS="$OPENSSL_CPPFLAGS -I/usr/kerberos/include"
                AC_SUBST(OPENSSL_CPPFLAGS)
            fi
        fi
    fi
])
