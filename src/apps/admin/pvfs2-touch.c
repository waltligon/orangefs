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
#include <assert.h>

#include "pvfs2.h"
#include "str-utils.h"
#include "bmi.h"
#include "pvfs2-usrint.h"
#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

/* optional parameters, filled in by parse_args() */
struct options
{
    int random;
    char* server_list;
    uint32_t num_files;
    char **filenames;
};

static struct options *parse_args(int argc, char **argv);
static void usage(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret = -1, i = 0;
    struct options *user_opts = NULL;
    PVFS_sys_layout layout;

    layout.algorithm = PVFS_SYS_LAYOUT_ROUND_ROBIN;
    layout.server_list.count = 0;
    layout.server_list.servers = NULL;

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

        layout.algorithm = PVFS_SYS_LAYOUT_ROUND_ROBIN;
        layout.server_list.count = 0;
        if(layout.server_list.servers)
        {
            free(layout.server_list.servers);
        }
        layout.server_list.servers = NULL;

        mode_t mode = (mode_t) PVFS_util_translate_mode(
                       (S_IROTH|S_IWOTH|S_IRGRP|S_IWGRP|S_IRUSR|S_IWUSR)
                       & ~PVFS_util_get_umask(), 0);

        rc = pvfs_creat(working_file, mode);
        if (rc < 0)
        {
            fprintf(stderr, "Error: An error occurred while creating %s\n",
                    working_file);
            perror("pvfs_creat");
            ret = -1;
            break;
        }
    }

    PVFS_sys_finalize();    

    if(user_opts->server_list)
    {
        free(layout.server_list.servers);
        free(user_opts->server_list);
    }
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
    char flags[] = "l:r?";
    struct options *tmp_opts = NULL;

    tmp_opts = (struct options *)malloc(sizeof(struct options));
    if(!tmp_opts)
    {
	return NULL;
    }
    memset(tmp_opts, 0, sizeof(struct options));

    tmp_opts->filenames = 0;
    tmp_opts->server_list = NULL;
    tmp_opts->random = 0;

    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt)
        {
	    case('?'):
		usage(argc, argv);
		exit(EXIT_FAILURE);
	    case('l'):
                tmp_opts->server_list = strdup(optarg);
                if(!tmp_opts->server_list)
                {
                    perror("strdup");
                    exit(EXIT_FAILURE);
                }
                break;
            case('r'):
                tmp_opts->random = 1;
                break;
	}
    }

    if(tmp_opts->random && tmp_opts->server_list)
    {
        fprintf(stderr, "Error: only one of -r or -l may be specified.\n");
        exit(EXIT_FAILURE);
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
    fprintf(stderr, "Usage: %s pvfs2_filename[s]\n", argv[0]);
    fprintf(stderr, "   optional arguments:\n");
    fprintf(stderr, "   -l   use list layout (requires comma separated list of servers)\n");
    fprintf(stderr, "   -r   use random layout\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

