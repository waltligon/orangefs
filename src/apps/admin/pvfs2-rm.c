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
    PVFS_sysresp_getattr resp_getattr;

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
        int num_segs;
        char *working_file = user_opts->filenames[i];
        char directory[PVFS_NAME_MAX];
        char filename[PVFS_SEGMENT_MAX];

        char pvfs_path[PVFS_NAME_MAX] = {0};
        PVFS_fs_id cur_fs;
        PVFS_sysresp_lookup resp_lookup;
        PVFS_credentials credentials;
        PVFS_object_ref parent_ref;
        int tmp_len = 0;

        PVFS_util_gen_credentials(&credentials);

        /* Translate path into pvfs2 relative path */
        rc = PVFS_util_resolve(working_file, &cur_fs, pvfs_path,
            PVFS_NAME_MAX);
        if(rc < 0)
        {
            PVFS_perror("PVFS_util_resolve", rc);
            ret = -1;
            break;
        }

        tmp_len = strlen(pvfs_path);
        if(pvfs_path[tmp_len - 1] == '/')
        {
            /* User requested removal of something with a trailing slash.
             * Strip slashes, but then confirm that the target is in fact a
             * directory, or else the request is invalid
             */
            while(tmp_len > 1 && pvfs_path[tmp_len - 1] == '/')
            {
                pvfs_path[tmp_len - 1] = '\0';
                tmp_len--;
            }

            memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
            rc = PVFS_sys_lookup(cur_fs, pvfs_path, &credentials,
                                 &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
            if (rc)
            {
                PVFS_perror("PVFS_sys_lookup", rc);
                ret = -1;
                break;
            }

            memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
            rc = PVFS_sys_getattr(resp_lookup.ref, PVFS_ATTR_SYS_TYPE,
                                   &credentials, &resp_getattr, NULL);
            if (rc)
            {
                PVFS_perror("PVFS_sys_getattr", rc);
                ret = -1;
                break;
            }
            if (resp_getattr.attr.objtype != PVFS_TYPE_DIRECTORY)
            {
                fprintf(stderr, "Error: object is not a directory.\n");
                ret = -1;
                break;
            }
        }

        /* break into file and directory */
        rc = PINT_get_base_dir(pvfs_path, directory, PVFS_NAME_MAX);
        if(rc < 0)
        {
            PVFS_perror("PINT_get_base_dir", rc);
            ret = -1;
            break;
        }
        num_segs = PINT_string_count_segments(pvfs_path);
        rc = PINT_get_path_element(pvfs_path, num_segs - 1,
                                   filename, PVFS_SEGMENT_MAX);

        if (rc)
        {
            PVFS_perror("PINT_get_path_element", rc);
            ret = -1;
            break;
        }

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        rc = PVFS_sys_lookup(cur_fs, directory, &credentials,
                             &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
        if (rc)
        {
            PVFS_perror("PVFS_sys_lookup", rc);
            ret = -1;
            break;
        }

        parent_ref = resp_lookup.ref;
        rc = PVFS_sys_remove(filename, parent_ref, &credentials, NULL);
        if (rc)
        {
            fprintf(stderr, "Error: An error occurred while "
                    "removing %s\n", working_file);
            PVFS_perror("PVFS_sys_remove", rc);
            ret = -1;
            break;
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

