AM_CPPFLAGS=-DVERSION=\""${PACKAGE_VERSION}"\" \
            -DPROVIDER_NAME=\"mod_authn_orangefs\" \
            "-I`${WP_APXS} -q INCLUDEDIR`" \
            "-I`${WP_APXS} -q APR_INCLUDEDIR`" \
            "`${WP_PVFS2_CONFIG} --cflags`" \
            "-I${WP_PVFS2_SOURCE}" \
            "-I${WP_PVFS2_SOURCE}/src/common/misc" \
            "-I${WP_PVFS2_SOURCE}/src/common/quickhash" \
            "-I${WP_PVFS2_SOURCE}/src/common/quicklist" \
            "-I${WP_PVFS2_SOURCE}/src/common/security" \
            "-I${WP_PVFS2_SOURCE}/src/io/bmi" \
            "-I${WP_PVFS2_SOURCE}/src/io/description" \
            "-I${WP_PVFS2_SOURCE}/src/io/trove" \
            "-I${WP_PVFS2_SOURCE}/src/proto"
AM_LDFLAGS="`${WP_PVFS2_CONFIG} --libs`"
lib_LTLIBRARIES = libmod_authn_orangefs.la
libmod_authn_orangefs_la_SOURCES=mod_authn_orangefs.c

install: libmod_authn_orangefs.la
	${WP_APXS} -i -a -n authn_orangefs libmod_authn_orangefs.la
