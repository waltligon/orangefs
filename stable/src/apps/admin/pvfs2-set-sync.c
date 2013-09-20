/*
 * Copyright © Acxiom Corporation, 2005
 *
 * based on pvfs2-set-mode:
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
    char* mnt_point;
    int mnt_point_set;
    int32_t meta_sync;
    int meta_sync_set;
    int32_t data_sync;
    int data_sync_set;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options* user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_credentials creds;
    struct PVFS_mgmt_setparam_value param_value;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    ret = PVFS_util_init_defaults();
    if(ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return(-1);
    }

    /* translate local path into pvfs2 relative path */
    ret = PVFS_util_resolve(user_opts->mnt_point,
        &cur_fs, pvfs_path, PVFS_NAME_MAX);
    if(ret < 0)
    {
	fprintf(stderr, "Error: could not find filesystem for %s in pvfstab\n", 
	    user_opts->mnt_point);
	return(-1);
    }

    PVFS_util_gen_credentials(&creds);

    param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
    param_value.u.value = user_opts->meta_sync;
    ret = PVFS_mgmt_setparam_all(cur_fs,
				 &creds,
				 PVFS_SERV_PARAM_SYNC_META,
                                 &param_value,
				 NULL,
				 NULL /* detailed errors */);
    if(ret < 0)
    {
        PVFS_perror("PVFS_mgmt_setparam_all", ret);
        return(-1);
    }

    param_value.type = PVFS_MGMT_PARAM_TYPE_UINT64;
    param_value.u.value = user_opts->data_sync;

    ret = PVFS_mgmt_setparam_all(cur_fs,
				 &creds,
				 PVFS_SERV_PARAM_SYNC_DATA,
                                 &param_value,
				 NULL,
				 NULL /* detailed errors */);
    if(ret < 0)
    {
        PVFS_perror("PVFS_mgmt_setparam_all", ret);
        return(-1);
    }

    PVFS_sys_finalize();

    return(0);
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    char flags[] = "vm:M:D:";
    int one_opt = 0;
    int len = 0;
    int tmp_int = 0;

    struct options* tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts){
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* look at command line arguments */
    while((one_opt = getopt(argc, argv, flags)) != EOF){
	switch(one_opt)
        {
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
            case('M'):
                ret = sscanf(optarg, "%d", &tmp_int);
                if(ret != 1 || (tmp_int != 0 && tmp_int != 1))
                {
                    free(tmp_opts);
                    return(NULL);
                }
                tmp_opts->meta_sync = tmp_int;
                tmp_opts->meta_sync_set = 1;
                break;
            case('D'):
                ret = sscanf(optarg, "%d", &tmp_int);
                if(ret != 1 || (tmp_int != 0 && tmp_int != 1))
                {
                    free(tmp_opts);
                    return(NULL);
                }
                tmp_opts->data_sync = tmp_int;
                tmp_opts->data_sync_set = 1;
                break;
	    case('m'):
		len = strlen(optarg)+1;
		tmp_opts->mnt_point = (char*)malloc(len+1);
		if(!tmp_opts->mnt_point)
		{
		    free(tmp_opts);
		    return(NULL);
		}
		memset(tmp_opts->mnt_point, 0, len+1);
		ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
		if(ret < 1){
		    free(tmp_opts);
		    return(NULL);
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
		exit(EXIT_FAILURE);
	}
    }

    if(tmp_opts->meta_sync_set != 1 || tmp_opts->data_sync_set != 1)
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }

    if(!tmp_opts->mnt_point_set)
    {
	if(tmp_opts->mnt_point)
	    free(tmp_opts->mnt_point);
	free(tmp_opts);
	return(NULL);
    }

    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s -m <fs_mount_point> -D [0|1] -M [0|1]\n",
	argv[0]);
    fprintf(stderr, "  -D   always implicitly sync file data (0=off, 1=on)\n");
    fprintf(stderr, "  -M   always implicitly sync metadata  (0=off, 1=on)\n");
    fprintf(stderr, "Example: %s -m /mnt/pvfs2 -M 1 -D 0\n",
	argv[0]);

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

