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
    char* tmp_server;
    int tmp_server_index;
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
        int num_segs;
        char *working_file = user_opts->filenames[i];
        char directory[PVFS_NAME_MAX];
        char filename[PVFS_SEGMENT_MAX];

        layout.algorithm = PVFS_SYS_LAYOUT_ROUND_ROBIN;
        layout.server_list.count = 0;
        if(layout.server_list.servers)
        {
            free(layout.server_list.servers);
        }
        layout.server_list.servers = NULL;

        char pvfs_path[PVFS_NAME_MAX] = {0};
        PVFS_fs_id cur_fs;
        PVFS_sysresp_lookup resp_lookup;
        PVFS_sysresp_create resp_create;
        PVFS_credentials credentials;
        PVFS_object_ref parent_ref;
        PVFS_sys_attr attr;

        /* Translate path into pvfs2 relative path */
        rc = PINT_get_base_dir(working_file, directory, PVFS_NAME_MAX);
        num_segs = PINT_string_count_segments(working_file);
        rc = PINT_get_path_element(working_file, num_segs - 1,
                                   filename, PVFS_SEGMENT_MAX);

        if (rc)
        {
            fprintf(stderr, "Unknown path format: %s\n", working_file);
            ret = -1;
            break;
        }

        rc = PVFS_util_resolve(directory, &cur_fs,
                               pvfs_path, PVFS_NAME_MAX);
        if (rc)
        {
            PVFS_perror("PVFS_util_resolve", rc);
            ret = -1;
            break;
        }

        PVFS_util_gen_credentials(&credentials);

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        rc = PVFS_sys_lookup(cur_fs, pvfs_path, &credentials,
                             &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
        if (rc)
        {
            PVFS_perror("PVFS_sys_lookup", rc);
            ret = -1;
            break;
        }

        /* Set attributes */
        memset(&attr, 0, sizeof(PVFS_sys_attr));
        attr.owner = credentials.uid;
        attr.group = credentials.gid;
        attr.perms = 0777;
        attr.atime = time(NULL);
        attr.mtime = attr.atime;
        attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
        attr.dfile_count = 0;

        parent_ref = resp_lookup.ref;

        if(user_opts->random)
        {
            layout.algorithm = PVFS_SYS_LAYOUT_RANDOM;
        }
        else if(user_opts->server_list)
        {
            layout.algorithm = PVFS_SYS_LAYOUT_LIST;
            layout.server_list.count = 1;
            tmp_server = user_opts->server_list;

            /* iterate once to count servers */
            while((tmp_server = index(tmp_server, ',')))
            {
                layout.server_list.count++;
                tmp_server++;
            }

            layout.server_list.servers = 
                malloc(layout.server_list.count*sizeof(PVFS_BMI_addr_t));
            if(!(layout.server_list.servers))
            {
                perror("malloc");
                ret = -1;
                break;
            }

            /* split servers out and resolve each addr */
            tmp_server_index = 0;
            for(tmp_server = strtok(user_opts->server_list, ","); 
                tmp_server != NULL;
                tmp_server = strtok(NULL, ","))
            {
                assert(tmp_server_index < layout.server_list.count);
                
                /* TODO: is there a way to do this without internal BMI
                 * functions?
                 */
                rc = BMI_addr_lookup(
                    &layout.server_list.servers[tmp_server_index],
                    tmp_server);
                if(rc < 0)
                {
                    PVFS_perror("BMI_addr_lookup", rc);
                    break;
                }
                tmp_server_index++;
            }
            if(tmp_server_index != layout.server_list.count)
            {
                fprintf(stderr, "Error: unable to resolve server list.\n");
                ret = -1;
                break;
            }
        }

        rc = PVFS_sys_create(filename,
                             parent_ref,
                             attr,
                             &credentials,
                             NULL,
                             &resp_create,
                             &layout,
                             NULL);
        if (rc)
        {
            fprintf(stderr, "Error: An error occurred while creating %s\n",
                    working_file);
            PVFS_perror("PVFS_sys_create", rc);
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

