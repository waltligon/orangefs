/*
 * Copyright 2015 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pvfs2.h"
#include "pvfs2-internal.h"
#include "pvfs2-mgmt.h"
#include "pint-cached-config.h"

void err(int, const char *, ...);
void errp(int, int, const char *, ...);
char *errv(const char *, va_list);
void errx(int, const char *, ...);

struct addr_array {
	PVFS_BMI_addr_t *addrs;
	int count;
};

struct addr_array *get_addr_array(PVFS_fs_id, const char *);

#define MODE_START 1
#define MODE_STOP 2

void usage(void);

/* Print error message with errno string. */
void
err(int ret, const char *fmt, ...)
{
	va_list ap;
	char *s;
	va_start(ap, fmt);
	s = errv(fmt, ap);
	perror(s);
	exit(ret);
}

/* Print error message with PVFS return error string. */
void
errp(int ret, int pvfs_err, const char *fmt, ...)
{
	va_list ap;
	char *s;
	va_start(ap, fmt);
	s = errv(fmt, ap);
	PVFS_perror(s, pvfs_err);
	exit(ret);
}

/* Allocate and format string for error message printers. */
char *
errv(const char *fmt, va_list ap1)
{
	va_list ap2;
	size_t len;
	char *s;
	va_copy(ap2, ap1);
	len = vsnprintf(NULL, 0, fmt, ap1);
	s = malloc(len+1);
	if (!s) {
		perror("malloc");
		exit(1);
	}
	vsnprintf(s, len+1, fmt, ap2);
	return s;
}

/* Print error message. */
void
errx(int ret, const char *fmt, ...)
{
	va_list ap;
	char *s;
	va_start(ap, fmt);
	s = errv(fmt, ap);
	fputs(s, stderr);
	exit(ret);
}

/* If addrs is not NULL, generate a list of PVFS servers referenced by it.
 * Otherwise generate a list of all PVFS servers. */
struct addr_array *
get_addr_array(PVFS_fs_id fs_id, const char *addrs)
{
	struct addr_array *aa;
	const char *s, *end;
	char addr[PVFS_PATH_MAX]; /* XXX: better size? */
	int ret, stop = 0, i;
	aa = malloc(sizeof *aa);
	if (!aa)
		err(1, "malloc");

	if (addrs) {
		/* Count addresses. */
		s = addrs;
		aa->count = 1;
		while ((s = strchr(s, ',')) != 0) {
			aa->count++;
			s++;
		}
		aa->addrs = calloc(aa->count, sizeof *aa->addrs);
		if (!aa->addrs)
			err(1, "malloc");

		/* Lookup addresses. */
		i = 0;
		s = addrs;
		do {
			end = strchr(s, ',');
			if (!end) {
				stop = 1;
				end = strchr(s, 0);
			}
			if (end-s+1 > PVFS_PATH_MAX)
				errx(1, "get_addr_array buffer too small");
			memcpy(addr, s, end-s);
			addr[end-s] = 0;
			BMI_addr_lookup(aa->addrs+i++, addr);
			if (!stop)
				s = end+1;
		} while (!stop);
	} else {
		ret = PVFS_mgmt_count_servers(fs_id, PINT_SERVER_TYPE_ALL,
		    &aa->count);
		if (ret < 0)
			errp(1, ret, "PVFS_mgmt_count_servers");
		if (aa->count <= 0)
			errx(1, "unable to load any server addresses");

		aa->addrs = calloc(aa->count, sizeof *aa->addrs);
		if (!aa->addrs)
			err(1, "malloc");
		ret = PVFS_mgmt_get_server_array(fs_id, PINT_SERVER_TYPE_ALL,
		    aa->addrs, &aa->count);
		if (ret < 0)
			errp(1, ret, "PVFS_mgmt_get_server_array");
	}
	return aa;
}

void
usage(void)
{
	fputs("usage: pvfs2-bgproc -s [ -a addrs ] name\n", stderr);
	fputs("usage: pvfs2-bgproc -k [ -a addrs ] pid\n", stderr);
	exit(1);
}

int
main(int argc, char **argv)
{
	char *addrs = NULL, *name;
	const PVFS_util_tab *tab;
	char path[PVFS_PATH_MAX];
	struct addr_array *aa;
	int mode = 0, ret;
	PVFS_fs_id fs_id;
	pid_t pid;
	size_t i;
	char ch;

	/* Parse command line. */
	while ((ch = getopt(argc, argv, "a:kls")) != -1) {
		switch (ch) {
		case 'a':
			addrs = optarg;
			break;
		case 'k':
			if (mode)
				usage();
			mode = MODE_STOP;
			break;
		case 's':
			if (mode)
				usage();
			mode = MODE_START;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (!mode) {
		usage();
	} else if (mode == MODE_START || mode == MODE_STOP) {
		if (!*argv)
			usage();
	}

	if (mode == MODE_START)
		name = *argv;
	else if (mode == MODE_STOP)
		pid = strtol(*argv, NULL, 10);

	/* Initialize PVFS. */
	ret = PVFS_util_init_defaults();
	if (ret < 0)
		errp(1, ret, "PVFS_util_init_defaults");
	tab = PVFS_util_parse_pvfstab(NULL);
	if (tab == NULL)
		errx(1, "there are no filesystems in the pvfstab\n", stderr);
	ret = PVFS_util_resolve(tab->mntent_array[0].mnt_dir, &fs_id, path,
	    PVFS_PATH_MAX);
	if (ret < 0) 
		errp(1, ret, "PVFS_util_resolve");
	if (*path == 0) {
		path[0] = '/';
		path[1] = 0;
	}

	aa = get_addr_array(fs_id, addrs);
	if (aa->count < 1) {
		fprintf(stderr, "no addresses were given\n");
		exit(1);
	}

	switch (mode) {
	case MODE_START:
		{int *statuses;
                pid_t *pids;
		statuses = calloc(aa->count, sizeof *statuses);
		if (!statuses)
			err(1, "malloc");
		pids = calloc(aa->count, sizeof *pids);
		if (!pids)
			err(1, "malloc");
		ret = PVFS_mgmt_parallel_start(fs_id, aa->count, aa->addrs,
		    statuses, pids, name);
		if (ret < 0)
			errp(1, ret, "PVFS_mgmt_parallel_start");
		for (i = 0; i < aa->count; i++)
			printf("%s %d %lu\n",
			    PVFS_mgmt_map_addr(fs_id, aa->addrs[i], NULL),
			    statuses[i], (unsigned long)pids[i]);
		break;}
	case MODE_STOP:
		{int status;
		ret = PVFS_mgmt_parallel_stop(fs_id, aa->addrs[0], &status,
		    pid);
		if (ret < 0)
			errp(1, ret, "PVFS_mgmt_parallel_stop");
		printf("%s %d\n",
		    PVFS_mgmt_map_addr(fs_id, aa->addrs[0], NULL),
		    status);
		break;}
	}
	return 0;
}
