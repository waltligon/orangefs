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

typedef struct
{
    int remove_object_only;
    PVFS_handle object_handle;
    PVFS_handle parent_handle;
    PVFS_fs_id fs_id;
    char dirent_name[PATH_MAX];
}  options_t;

static void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s [OPTION] ...\n\n", argv[0]);
    fprintf(stderr, "-h, --help                  display this help "
            "information and exit\n");
    fprintf(stderr, "-v, --version               display version "
            "information and exit\n");
    fprintf(stderr, "-f, --fsid=<fs_id>          use the specified "
            "fs_id for the specified handle\n");
    fprintf(stderr, "-o, --object=<handle>       remove the specified "
            "object handle\n");
    fprintf(stderr, "-p, --parent=<handle>       use the specified parent "
            "handle\n\t\t\t\t(requires -d, or --dirent option)\n");
    fprintf(stderr, "-d, --dirent=<entry_name>   use the specified dirent "
            "name\n\t\t\t\t(requires -p, or --parent option)\n");
    fprintf(stderr, "\nNOTE:  The -f, or --fsid option must ALWAYS "
            "be specified\n");
}

static options_t *parse_args(int argc, char **argv)
{
    int ret = 0, option_index = 0;
    char *cur_option = NULL;
    options_t *tmp_opts = NULL;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"version",0,0,0},
        {"object",1,0,0},
        {"parent",1,0,0},
        {"dirent",1,0,0},
        {"fsid",1,0,0},
        {0,0,0,0}
    };

    if (argc == 1)
    {
        return NULL;
    }

    tmp_opts = (options_t *)malloc(sizeof(options_t));
    if (!tmp_opts)
    {
	return NULL;
    }
    memset(tmp_opts, 0, sizeof(options_t));

    while((ret = getopt_long(argc, argv, "hvo:p:d:f:",
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
                else if (strcmp("object", cur_option) == 0)
                {
                    goto do_object;
                }
                else if (strcmp("parent", cur_option) == 0)
                {
                    goto do_parent;
                }
                else if (strcmp("dirent", cur_option) == 0)
                {
                    goto do_dirent;
                }
                else if (strcmp("fsid", cur_option) == 0)
                {
                    goto do_fsid;
                }
                else
                {
                    usage(argc, argv);
                    exit(1);
                }
                break;
            case 'h':
          do_help:
                usage(argc, argv);
                exit(0);
                break;
	    case 'v':
          do_version:
                printf("%s\n", PVFS2_VERSION);
                exit(0);
		break;
	    case 'o':
          do_object:
                tmp_opts->remove_object_only = 1;
                tmp_opts->object_handle =
                    (PVFS_handle)strtoull(optarg, NULL,10);
		break;
	    case 'p':
          do_parent:
                tmp_opts->parent_handle =
                    (PVFS_handle)strtoull(optarg, NULL,10);
		break;
            case 'd':
          do_dirent:
                snprintf(tmp_opts->dirent_name, PATH_MAX, optarg);
                break;
	    case 'f':
          do_fsid:
                tmp_opts->fs_id =
                    (PVFS_fs_id)strtoull(optarg, NULL,10);
		break;
	    case '?':
		usage(argc, argv);
		exit(1);
	}
    }
    return tmp_opts;
}

int main(int argc, char **argv)
{
    int ret = -1;
    options_t *user_opts = NULL;
    PVFS_object_ref ref;
    PVFS_credentials credentials;

    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
        usage(argc, argv);
	return ret;
    }

    ref.fs_id = user_opts->fs_id;
    if (user_opts->remove_object_only)
    {
        ref.handle = user_opts->object_handle;
        if ((ref.handle == PVFS_HANDLE_NULL) ||
            (ref.fs_id == PVFS_FS_ID_NULL))
        {
            fprintf(stderr, "Invalid object reference specified: "
                    "%Lu,%d\n", Lu(ref.handle), ref.fs_id);
            return ret;
        }
    }
    else
    {
        ref.handle = user_opts->parent_handle;
        if ((ref.handle == PVFS_HANDLE_NULL) ||
            (ref.fs_id == PVFS_FS_ID_NULL))
        {
            fprintf(stderr, "Invalid parent reference specified: "
                    "%Lu,%d\n", Lu(ref.handle), ref.fs_id);
            return ret;
        }

        if (!user_opts->dirent_name)
        {
            fprintf(stderr, "No dirent name specified under parent "
                    "%Lu,%d\n", Lu(ref.handle), ref.fs_id);
            return ret;
        }
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return -1;
    }

    PVFS_util_gen_credentials(&credentials);

    if (user_opts->remove_object_only)
    {
        fprintf(stderr,"Attempting to remove object %Lu,%d\n",
                Lu(ref.handle), ref.fs_id);

        ret = PVFS_mgmt_remove_object(ref, &credentials);
        if (ret)
        {
            PVFS_perror("PVFS_mgmt_remove_object", ret);
        }
    }
    else
    {
        fprintf(stderr,"Attempting to remove dirent \"%s\" under %Lu,%d"
                "\n", user_opts->dirent_name, Lu(ref.handle), ref.fs_id);

        ret = PVFS_mgmt_remove_dirent(
            ref, user_opts->dirent_name, &credentials);
        if (ret)
        {
            PVFS_perror("PVFS_mgmt_remove_dirent", ret);
        }
    }

    free(user_opts);
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
