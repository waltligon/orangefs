/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <pvfs2-bgproc.h>

/*
 * Ensure that the queue is ready to be used, i.e. that the queue
 * directory qdir exists and is empty and that the random number
 * generator is ready.
 */
int setup_queue(char *qdir)
{
	char cur[PATH_MAX], tmp[PATH_MAX];
	char *dir;
	int r;

	snprintf(cur, PATH_MAX, "%s/cur", qdir);
	snprintf(tmp, PATH_MAX, "%s/tmp", qdir);

	srand(time(NULL));

	dir = qdir;
	r = mkdir(dir, 0777);
	if (r != 0 && errno != EEXIST)
		goto fail;

	dir = cur;
	r = mkdir(dir, 0777);
	if (r != 0 && errno != EEXIST)
		goto fail;

	dir = tmp;
	r = mkdir(dir, 0777);
	if (r != 0 && errno != EEXIST)
		goto fail;

	return 0;
fail:
	bgproc_err("could not create directory '%s': %s", dir,
	        strerror(errno));
	return 1;
}

/*
 * Add data to the queue qdir.
 */
int enqueue(char *qdir, char *data)
{
	char name[NAME_MAX], tmp[PATH_MAX], cur[PATH_MAX];
	FILE *f = NULL;

	snprintf(name, NAME_MAX, "%10ju%05d",
	         (uintmax_t)time(NULL), rand()%100000);
	snprintf(tmp, PATH_MAX, "%s/tmp/%s", qdir, name);
	snprintf(cur, PATH_MAX, "%s/cur/%s", qdir, name);

	f = fopen(tmp, "w");
	if (f == NULL) {
		bgproc_err("could not open '%s': %s",
		        tmp, strerror(errno));
		goto fail;
	}
	if (fprintf(f, "%s\n", data) == -1) {
		bgproc_err("could not write to '%s': %s",
		        tmp, strerror(errno));
		goto fail;
	}
	fclose(f);
	f = NULL;

	if (rename(tmp, cur) != 0) {
		bgproc_err("could not rename '%s' to '%s': %s",
		        tmp, cur, strerror(errno));
		goto fail;
	}

	return 0;
fail:
	if (f != NULL)
		fclose(f);
	return 1;
}

/*
 * Remove data from the queue qdir. The pointer stored in data will have
 * to be freed.
 */
int dequeue(char *qdir, char **data)
{
	char cur[PATH_MAX];
	DIR *d = NULL;
	struct dirent *de;
	FILE *f = NULL;
	int found = 0;
	char tmp[PATH_MAX];

	snprintf(cur, PATH_MAX, "%s/cur/", qdir);

	d = opendir(cur);
	if (d == NULL) {
		bgproc_err("could not open '%s': %s",
		        cur, strerror(errno));
		goto fail;
	}

	/* Loop so special files can be skipped. */
	while ((de = readdir(d))) {
		char path[PATH_MAX];
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0) {
			continue;
		}
		/* Read path from any file in queue. */
		snprintf(path, PATH_MAX, "%s/cur/%s", qdir, de->d_name);
		f = fopen(path, "r");
		if (f == NULL) {
			bgproc_err("could not open '%s': %s",
			        path, strerror(errno));
			goto fail;
		}
		if (fgets(tmp, PATH_MAX, f) == NULL) {
			bgproc_err("could not read from '%s': "
			        "%s", path, strerror(errno));
			goto fail;
		}
		fclose(f);
		f = NULL;
		/* Remove file path was read from. */
		if (unlink(path) != 0) {
			bgproc_err("could not unlink '%s': %s",
			        path, strerror(errno));
			goto fail;
		}
		found = 1;
		break;
	}
	closedir(d);
	d = NULL;

	if (found) {
		*(strrchr(tmp, '\n')) = 0;
		if (data != NULL)
			*data = strdup(tmp);
	} else {
		if (data != NULL)
			*data = NULL;
	}

	return 0;
fail:
	if (f != NULL)
		fclose(f);
	if (d != NULL)
		closedir(d);
	return 1;
}

/*
 * qdir = queue directory
 * rdir = record directory
 */
char *qdir = NULL, *rdir = NULL;

int record_file(char *data, off_t size)
{
/*	bgproc_log("file %s %d", data, size);*/
	return 0;
}

int record_dir(char *data, unsigned int nfiles, off_t size)
{
	bgproc_log("%s contains %d files which are %d bytes",
	           data, nfiles, size);
	return 0;
}

/*
 * Check the size of the file data and record it the file data. Also
 * add the file size to the pointer nbytes.
 */
int process_file(char *data, off_t *nbytes)
{
	struct stat sb;
	if (stat(data, &sb) != 0) {
		bgproc_err("could not stat '%s': %s",
		        data, strerror(errno));
		return 1;
	}
	*nbytes += sb.st_size;
	if (record_file(data, sb.st_size) != 0) {
		bgproc_err("could not record '%s'", data);
		return 1;
	}
	return 0;
}

/*
 * Add the directory children of the directory data to the queue.
 */
void enqueue_children(char *path)
{
	DIR *d = NULL;
	struct dirent *de;
	unsigned int nfiles = 0;
	off_t nbytes = 0;

	d = opendir(path);
	if (d == NULL) {
		bgproc_err("could not open '%s': %s",
		           path, strerror(errno));
		goto fail;
	}

	while ((de = readdir(d))) {
		char new[PATH_MAX];
		struct stat sb;
		/* Skip special entries. */
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0) {
			continue;
		}
		/* Avoid adding two slashes when path is '/'. */ 
		if (strcmp(path, "/") == 0)
			snprintf(new, PATH_MAX, "/%s", de->d_name);
		else
			snprintf(new, PATH_MAX, "%s/%s", path, de->d_name);
		if (stat(new, &sb) != 0) {
			bgproc_err("could not stat '%s': %s",
			        new, strerror(errno));
			continue;
		}
		if (S_ISDIR(sb.st_mode)) {
			enqueue(qdir, new);
		} else if (S_ISREG(sb.st_mode)) {
			process_file(new, &nbytes);
			nfiles++;
		}
	}
	if (record_dir(path, nfiles, nbytes) != 0) {
		bgproc_err("could not record '%s'", path);
		goto fail;
	}

fail:
	if (d != NULL)
		closedir(d);
}

int main(int argc, char *argv[])
{
	int r;

	if ((r = bgproc_start(argc, argv)) != 0) {
		return r;
	}

	qdir = bgproc_getarg("qdir");
	if (qdir == NULL) {
		bgproc_err("no qdir (queue directory) argument");
		return 1;
	}

	rdir = bgproc_getarg("rdir");
	if (rdir == NULL) {
		bgproc_err("no rdir (record directory) argument");
		return 1;
	}

	if (setup_queue(qdir) != 0) {
		bgproc_err("could not setup queue");
		return 1;
	}

	while (1) {
		char *data;
		if (dequeue(qdir, &data) != 0) {
			bgproc_err("could not dequeue");
			return 1;
		}
		if (data) {
			enqueue_children(data);
			free(data);
		} else {
			sleep(1);
			continue;
		}
	}

	return 0;
}
