/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "pvfs2-usrint.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
    int force;
    int recursive;
    uint32_t num_files;
    char **filenames;
};

static struct options *parse_args(int argc, char **argv);
static void usage(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret = -1, i = 0;
    struct options *user_opts = NULL;
    //PVFS_sysresp_getattr resp_getattr;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	return(-1);
    }

    /* Initialize the pvfs2 server */
    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return -1;
    }

    /* Remove each specified file */
    for (i = 0; i < user_opts->num_files; ++i)
    {
        int rc;
        char *working_file = user_opts->filenames[i];

        PVFS_credential credentials;

        rc = PVFS_util_gen_credential_defaults(&credentials);
        if (rc < 0)
        {
            PVFS_perror("PVFS_util_gen_credential_defaults", ret);
            ret = -1;
            break;
        }

	struct stat stat_buf;
	memset(&stat_buf, 0 , sizeof(struct stat));
	pvfs_stat(working_file, &stat_buf);
	if (S_ISDIR(stat_buf.st_mode))
	{
            rc = pvfs_rmdir(working_file);
	    if (rc) 
	    {
	        perror("PVFS_rmdir");
	        ret = -1;
	        break;
	    }
	}
	else
        {
	    rc = pvfs_unlink(working_file);
	    if (rc)
    	    {
		perror("PVFS_unlink");
		ret = -1;
		break;
	    }
        }
    }

    PVFS_sys_finalize();
    free(user_opts);
    return ret;
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char **argv)
{
    int one_opt = 0;
    char flags[] = "rf?";
    struct options *tmp_opts = NULL;

    tmp_opts = (struct options *)malloc(sizeof(struct options));
    if(!tmp_opts)
    {
	return NULL;
    }
    memset(tmp_opts, 0, sizeof(struct options));

    tmp_opts->force = 0;
    tmp_opts->recursive = 0;
    tmp_opts->filenames = 0;

    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt)
        {
	    case('f'):
		fprintf(stderr, "Error: force option not supported.\n");
		free(tmp_opts);
		return NULL;
	    case('r'):
		fprintf(stderr, "Error: recursive option not "
                        "supported.\n");
		free(tmp_opts);
		return NULL;
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    if (optind < argc)
    {
        int i = 0;
        tmp_opts->num_files = argc - optind;
        tmp_opts->filenames = malloc((argc - optind) * sizeof(char*));
        while (optind < argc)
        {
            tmp_opts->filenames[i] = argv[optind];
            optind++;
            i++;
        }
    }
    else
    {
	usage(argc, argv);
	exit(EXIT_FAILURE);
    }
    return tmp_opts;
}


static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s [-rf] pvfs2_filename[s]\n", argv[0]);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

