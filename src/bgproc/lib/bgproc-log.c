/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <pvfs2.h>
#include <pvfs2-usrint.h>
#include <pvfs2-bgproc.h>

static FILE *bgproc_log = NULL;

int bgproc_printf(char *fmt, ...)
{
	char path[PATH_MAX];
	va_list ap;
	time_t t;
	char *s;
	/* open the log if necessary */
	if (bgproc_log == NULL) {
		snprintf(path, PATH_MAX, "%s/log", bgproc_getdir());
		bgproc_log = fopen(path, "a");
		if (bgproc_log == NULL)
			return 1;
	}
	/* output time and message */
	va_start(ap, fmt);
	t = time(NULL);
	s = ctime(&t);
	*(strrchr(s, '\n')) = 0;
	fprintf(bgproc_log, "[%s] ", s);
	vfprintf(bgproc_log, fmt, ap);
	putc('\n', bgproc_log);
	fflush(bgproc_log);
	va_end(ap);
	return 0;
}
