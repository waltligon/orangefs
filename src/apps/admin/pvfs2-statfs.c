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

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char* mnt_point;
    int mnt_point_set;
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    pvfs_mntlist mnt = {0,NULL};
    struct options* user_opts = NULL;
    int mnt_index = -1;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_statfs resp_statfs;
    int i;
    PVFS_credentials creds;
    struct PVFS_mgmt_server_stat* stat_array = NULL;
    int outcount;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return(-1);
    }

    /* look at pvfstab */
    if(PVFS_util_parse_pvfstab(&mnt))
    {
        fprintf(stderr, "Error: failed to parse pvfstab.\n");
        return(-1);
    }

    /* see if the destination resides on any of the file systems
     * listed in the pvfstab; find the pvfs fs relative path
     */
    for(i=0; i<mnt.ptab_count; i++)
    {
	ret = PVFS_util_remove_dir_prefix(user_opts->mnt_point,
	    mnt.ptab_array[i].mnt_dir, pvfs_path, PVFS_NAME_MAX);
	if(ret == 0)
	{
	    mnt_index = i;
	    break;
	}
    }

    if(mnt_index == -1)
    {
	fprintf(stderr, "Error: could not find filesystem for %s in pvfstab\n", 
	    user_opts->mnt_point);
	return(-1);
    }

    memset(&resp_init, 0, sizeof(resp_init));
    ret = PVFS_sys_initialize(mnt, 0, &resp_init);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_initialize", ret);
	return(-1);
    }

    cur_fs = resp_init.fsid_list[mnt_index];

    creds.uid = getuid();
    creds.gid = getgid();

    /* gather normal statfs statistics from system interface */
    ret = PVFS_sys_statfs(cur_fs, creds, &resp_statfs);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_statfs", ret);
	return(-1);
    }

    printf("\naggregate statistics:\n");
    printf("---------------------------------------\n\n");
    printf("\tfs_id: %d\n", (int)resp_statfs.statfs_buf.fs_id);
    printf("\tnumber of servers: %d\n", resp_statfs.server_count);
    printf("\tbytes available: %16Ld\n", 
	(long long)resp_statfs.statfs_buf.bytes_available);
    printf("\tbytes total:     %16Ld\n", 
	(long long)resp_statfs.statfs_buf.bytes_total);
    printf("\n");

    /* now call management functions to determine per-server statistics */
    stat_array = (struct PVFS_mgmt_server_stat*)malloc(resp_statfs.server_count
	* sizeof(struct PVFS_mgmt_server_stat));
    if(!stat_array)
    {
	perror("malloc");
	return(-1);
    }

    outcount = resp_statfs.server_count;
    ret = PVFS_mgmt_statfs_all(cur_fs, creds, stat_array, &outcount);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_statfs_all()", ret);
	return(-1);
    }

    /* sanity check */
    if(outcount != resp_statfs.server_count)
    {
	fprintf(stderr, "Error: PVFS_mgmt_statfs_all() returned bad results.\n");
	return(-1);
    }

    printf("\nindividual server statistics:\n");
    printf("---------------------------------------\n\n");

    for(i=0; i<outcount; i++)
    {
	printf("server: %s\n", stat_array[i].bmi_address);
	printf("\tbytes available: %16Ld\n", (long long)stat_array[i].bytes_available);
	printf("\tbytes total:     %16Ld\n", (long long)stat_array[i].bytes_total);
	if(stat_array[i].server_type & (PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER))
	    printf("\tmode: serving both metadata and I/O data\n");
	else if(stat_array[i].server_type & PVFS_MGMT_IO_SERVER)
	    printf("\tmode: serving only I/O data\n");
	else if(stat_array[i].server_type & PVFS_MGMT_META_SERVER)
	    printf("\tmode: serving only metadata\n");
	
	printf("\n");
    }

    return(ret);
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    /* getopt stuff */
    extern char* optarg;
    extern int optind, opterr, optopt;
    char flags[] = "vm:";
    int one_opt = 0;
    int len = 0;

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

    if(!tmp_opts->mnt_point_set)
    {
	free(tmp_opts);
	return(NULL);
    }

    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s [-m fs_mount_point]\n",
	argv[0]);
    fprintf(stderr, "Example: %s -m /mnt/pvfs2\n",
	argv[0]);
    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

