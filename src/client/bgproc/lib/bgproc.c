/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include <pvfs2.h>
#include <pvfs2-usrint.h>

static char *bgproc_dir = NULL;
static FILE *bgproc_log = NULL;

int bgproc_start(int argc, char *argv[])
{
	struct stat sb;
	int r;

	if (argc < 2) {
		fprintf(stderr, "usage: bgproc [dir]\n");
		return 1;
	}
	bgproc_dir = strdup(argv[1]);

	if ((r = pvfs_stat(bgproc_dir, &sb)) == 0) {
		if (!(sb.st_mode & S_IFDIR)) {
			fprintf(stderr, "path %s is not a directory\n",
			        bgproc_dir);
			free(bgproc_dir);
			bgproc_dir = NULL;
			return 1;
		}
	}
	if (r != 0) {
		if (errno == ENOENT) {
			r = pvfs_mkdir(bgproc_dir, 0777);
			if (r != 0) {
				fprintf(stderr, "mkdir failed\n");
				return 1;
			}
		} else {
			fprintf(stderr, "stat failed\n");
			return 1;
		}
	}

	return 0;
}

int bgproc_log_printf(char *fmt, ...)
{
	char path[PATH_MAX];
	time_t t;
        char *s;

	if (bgproc_log == NULL) {
		snprintf(path, PATH_MAX, "%s/log", bgproc_dir);
		bgproc_log = fopen(path, "a");
		if (bgproc_log == NULL)
			return -1;
	}

        va_list ap;
        va_start(ap, fmt);
        t = time(NULL);
        s = ctime(&t);
        *(strrchr(s, '\n')) = 0;
        fprintf(bgproc_log, "[%s]", s);
        vfprintf(bgproc_log, fmt, ap);
        putc('\n', bgproc_log);
        fflush(bgproc_log);
        va_end(ap);
	return 0;
}
