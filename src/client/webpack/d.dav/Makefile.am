AM_CPPFLAGS=-DVERSION=\""${PACKAGE_VERSION}"\" \
            -DPROVIDER_NAME=\"mod_dav_orangefs\" \
            "-I`${WP_APXS} -q INCLUDEDIR`" \
            "-I`${WP_APXS} -q APR_INCLUDEDIR`" \
            "`${WP_PVFS2_CONFIG} --cflags`"
AM_LDFLAGS="`${WP_PVFS2_CONFIG} --libs`" -lpam
lib_LTLIBRARIES = libmod_dav_orangefs.la
libmod_dav_orangefs_la_SOURCES=mod_dav_orangefs.c

install: libmod_dav_orangefs.la
	${WP_APXS} -i -a -n dav_orangefs libmod_dav_orangefs.la
	(cd .. && AWK="${AWK}" WP_APXS="${WP_APXS}" sh pvfsinit.sh)
