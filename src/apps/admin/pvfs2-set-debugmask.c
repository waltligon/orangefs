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
#include "pvfs2-mgmt.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char *mnt_point;
    int mnt_point_set;
    uint64_t debug_mask;
    int debug_mask_set;
    char *single_server;
};

static struct options *parse_args(int argc, char **argv);
static void usage(int argc, char **argv);

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options *user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_credentials creds;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
        fprintf(stderr, "Error: failed to parse command line arguments.\n");
        usage(argc, argv);
        return(-1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->mnt_point,
                            &cur_fs, pvfs_path, PVFS_NAME_MAX);
    if (ret < 0)
    {
        fprintf(stderr, "Error: could not find filesystem "
                "for %s in pvfstab\n",  user_opts->mnt_point);
        return(-1);
    }

    PVFS_util_gen_credentials(&creds);

    if (user_opts->single_server)
    {
        printf("Setting debugmask on server %s\n",
               user_opts->single_server);

        ret = PVFS_mgmt_setparam_single(
            cur_fs, &creds, PVFS_SERV_PARAM_GOSSIP_MASK,
            user_opts->debug_mask, user_opts->single_server,
            NULL, NULL);
    }
    else
    {
        printf("Setting debugmask on all servers\n");

        ret = PVFS_mgmt_setparam_all(
            cur_fs, &creds, PVFS_SERV_PARAM_GOSSIP_MASK,
            user_opts->debug_mask, NULL, NULL);
    }

    if (ret)
    {
        char buf[64] = {0};
        PVFS_strerror_r(ret, buf, 64);
        fprintf(stderr, "Setparam failure: %s\n", buf);
    }
    return PVFS_sys_finalize();
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options *parse_args(int argc, char **argv)
{
    int ret = -1, len = 0, option_index = 0;
    char *cur_option = NULL;
    struct options *tmp_opts = NULL;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"mount",1,0,0},
        {"server",1,0,0},
        {0,0,0,0}
    };

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if (!tmp_opts)
    {
        return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* fill in defaults (except for hostid) */
    tmp_opts->debug_mask = 0;

    while((ret = getopt_long(argc, argv, "hvm:s:",
                             long_opts, &option_index)) != -1)
    {
        switch(ret)
        {
            case 0:
                cur_option = (char *)long_opts[option_index].name;

                if (strcmp("help", cur_option) == 0)
                {
                    goto do_help;
                }
                else if (strcmp("version", cur_option) == 0)
                {
                    goto do_version;
                }
                else if (strcmp("mount", cur_option) == 0)
                {
                    goto do_mount;
                }
                else if (strcmp("server", cur_option) == 0)
                {
                    goto do_server;
                }
                else
                {
                    usage(argc, argv);
                    exit(-1);
                }
                break;
            case('h'):
          do_help:
                usage(argc, argv);
                exit(0);
            case('v'):
          do_version:
                printf("%s\n", PVFS2_VERSION);
                exit(0);
            case('s'):
          do_server:
                tmp_opts->single_server = strdup(optarg);
                if (!tmp_opts)
                {
                    return NULL;
                }
                break;
            case('m'):
          do_mount:
                len = strlen(optarg) + 1;
                tmp_opts->mnt_point = (char*)malloc(len+1);
                if (!tmp_opts->mnt_point)
                {
                    free(tmp_opts);
                    return NULL;
                }
                memset(tmp_opts->mnt_point, 0, len+1);
                ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
                if(ret < 1)
                {
                    free(tmp_opts);
                    return NULL;
                }
                /* TODO: dirty hack... fix later.  The remove_dir_prefix()
                 * function expects some trailing segments or at least
                 * a slash off of the mount point
                 */
                strcat(tmp_opts->mnt_point, "/");
                tmp_opts->mnt_point_set = 1;
                break;
            case('?'):
                usage(argc, argv);
                exit(-1);
        }
    }

    if (optind != (argc - 1))
    {
        usage(argc, argv);
        exit(-1);
    }

    tmp_opts->debug_mask = PVFS_debug_eventlog_to_mask(argv[argc-1]);
    tmp_opts->debug_mask_set = 1;

    if (!tmp_opts->mnt_point_set)
    {
        free(tmp_opts);
        tmp_opts = NULL;
    }
    return tmp_opts;
}


static void usage(int argc, char **argv)
{
    int i = 0;
    char *mask = NULL;

    fprintf(stderr, "Usage: %s [OPTION] <mask_list>\n\n",
            argv[0]);
    fprintf(stderr, "-h, --help                  display this help "
            "information and exit\n");
    fprintf(stderr, "-v, --version               display version "
            "information and exit\n");
    fprintf(stderr, "-m, --mount=<mount_point>   use the specified "
            "mount point\n");
    fprintf(stderr, "-s, --server=<server_addr>  use the specified "
            "server address\n");
    fprintf(stderr, "\nExamples:\n  %s -m /mnt/pvfs2 \"network,server"
            "\"\n", argv[0]);
    fprintf(stderr, "\t[ enables network and server debugging on all "
            "servers ]\n\n");
    fprintf(stderr, "  %s -m /mnt/pvfs2 \"none\" -s \"tcp://localhost:"
            "3334/pvfs2-fs\"\n", argv[0]);
    fprintf(stderr, "\t[ disable debugging on the specified server only "
            " ]\n\n");
    fprintf(stderr, "Available masks include:\n");

    while((mask = PVFS_debug_get_next_debug_keyword(i++)) != NULL)
    {
        if (strlen(mask) < 6)
        {
            fprintf(stderr,"\t%s  \t",mask);
        }
        else
        {
            fprintf(stderr,"\t%s  ",mask);
        }

        if ((i % 4) == 0)
        {
            fprintf(stderr,"\n");
        }
    }
    fprintf(stderr, "\n");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
