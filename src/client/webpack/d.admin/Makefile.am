AM_CPPFLAGS=-DVERSION=\""${PACKAGE_VERSION}"\" \
            -DPROVIDER_NAME=\"mod_orangefs_admin\" \
            "-I`${WP_APXS} -q INCLUDEDIR`" \
            "-I`${WP_APXS} -q APR_INCLUDEDIR`" \
            "`${WP_PVFS2_CONFIG} --cflags`" \
            "-I${WP_PVFS2_SOURCE}" \
            "-I${WP_PVFS2_SOURCE}/src/common/misc" \
            "-I${WP_PVFS2_SOURCE}/src/common/quickhash" \
            "-I${WP_PVFS2_SOURCE}/src/common/quicklist" \
            "-I${WP_PVFS2_SOURCE}/src/io/description"
AM_LDFLAGS="`${WP_PVFS2_CONFIG} --libs`"
lib_LTLIBRARIES = libmod_orangefs_admin.la
libmod_orangefs_admin_la_SOURCES=mod_orangefs_admin.c jsmn.c

install: libmod_orangefs_admin.la
	${WP_APXS} -i -a -n orangefs_admin libmod_orangefs_admin.la
	(cd .. && AWK="${AWK}" WP_APXS="${WP_APXS}" sh pvfsinit.sh)
