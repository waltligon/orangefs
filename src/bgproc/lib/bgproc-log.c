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

static FILE *log = NULL;

int bgproc_err(char *fmt, ...)
{
	char path[PATH_MAX];
	va_list ap;
	time_t t;
	char *s;
	/* open the log if necessary */
	if (log == NULL) {
		snprintf(path, PATH_MAX, "%s/log", bgproc_getdir());
		log = fopen(path, "a");
		if (log == NULL)
			return 1;
	}
	/* output time and message */
	va_start(ap, fmt);
	t = time(NULL);
	s = ctime(&t);
	*(strrchr(s, '\n')) = 0;
	fprintf(log, "[%s] ERROR: ", s);
	vfprintf(log, fmt, ap);
	putc('\n', log);
	fflush(log);
	va_end(ap);
	return 0;
}

int bgproc_log(char *fmt, ...)
{
	char path[PATH_MAX];
	va_list ap;
	time_t t;
	char *s;
	/* open the log if necessary */
	if (log == NULL) {
		snprintf(path, PATH_MAX, "%s/log", bgproc_getdir());
		log = fopen(path, "a");
		if (log == NULL)
			return 1;
	}
	/* output time and message */
	va_start(ap, fmt);
	t = time(NULL);
	s = ctime(&t);
	*(strrchr(s, '\n')) = 0;
	fprintf(log, "[%s] ", s);
	vfprintf(log, fmt, ap);
	putc('\n', log);
	fflush(log);
	va_end(ap);
	return 0;
}
