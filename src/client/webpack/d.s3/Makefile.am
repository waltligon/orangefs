AM_CPPFLAGS=-DVERSION=\""${PACKAGE_VERSION}"\" \
            -DPROVIDER_NAME=\"mod_orangefs_s3\" \
            "-I`${WP_APXS} -q INCLUDEDIR`" \
            "-I`${WP_APXS} -q APR_INCLUDEDIR`" \
            "`${WP_PVFS2_CONFIG} --cflags`" \
            "`${WP_XML2_CONFIG} --cflags`"
AM_LDFLAGS="`${WP_PVFS2_CONFIG} --libs`" "`${WP_XML2_CONFIG} --libs`"
lib_LTLIBRARIES = libmod_orangefs_s3.la
libmod_orangefs_s3_la_SOURCES=mod_orangefs_s3.c

install: libmod_orangefs_s3.la
	${WP_APXS} -i -a -n orangefs_s3 libmod_orangefs_s3.la
	(cd .. && AWK="${AWK}" WP_APXS="${WP_APXS}" sh pvfsinit.sh)
