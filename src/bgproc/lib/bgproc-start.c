/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <pvfs2.h>
#include <pvfs2-usrint.h>
#include <pvfs2-bgproc.h>

static char *bgproc_dir = NULL;

/*
 * Initialize structures and files and perform sanity checks for
 * background process operation.
 */
int bgproc_start(int argc, char *argv[])
{
	struct stat sb;
	int r;
	if (argc < 2) {
		fprintf(stderr, "usage: bgproc [dir]\n");
		return 1;
	}
	bgproc_dir = strdup(argv[1]);
	/* abort if the path exists and is not a directory */
	if ((r = pvfs_stat(bgproc_dir, &sb)) == 0) {
		if (!(sb.st_mode & S_IFDIR)) {
			fprintf(stderr, "path '%s' is not a "
			        "directory\n", bgproc_dir);
			free(bgproc_dir);
			bgproc_dir = NULL;
			return 1;
		}
	}
	/* create the directory and abort if there is a failure */
	if (r != 0) {
		if (errno == ENOENT) {
			r = pvfs_mkdir(bgproc_dir, 0777);
			if (r != 0) {
				perror("mkdir failed");
				return 1;
			}
		} else {
			perror("stat failed");
			return 1;
		}
	}
	return 0;
}

char *bgproc_getdir(void)
{
	return bgproc_dir;
}

void bgproc_setdir(char *dir)
{
	if (bgproc_dir != NULL)
		free(bgproc_dir);
	bgproc_dir = strdup(dir);
}
