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
#include <limits.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"

#define HANDLE_BATCH 1000

/* TODO: this is a hack, define this stuff nicely somewhere that can
 * stay in sync with trove
 */
#define PVFS_ITERATE_START (INT_MAX-1)
#define PVFS_ITERATE_END (INT_MAX-2)

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
    int i,j;
    PVFS_credentials creds;
    int server_count;
    PVFS_id_gen_t* addr_array;
    int tmp_type;
    PVFS_handle** handle_matrix;
    int* handle_count_array;
    PVFS_ds_position* position_array;
    int more_flag = 0;

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

    /* count how many servers we have */
    ret = PVFS_mgmt_count_servers(cur_fs, creds, 
	PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
	&server_count);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_count_servers", ret);
	return(-1);
    }

    /* allocate a 2 dimensional array for handles */
    handle_matrix = (PVFS_handle**)malloc(
	server_count*sizeof(PVFS_handle));
    if(!handle_matrix)
    {
	perror("malloc");
	return(-1);
    }
    for(i=0; i<server_count; i++)
    {
	handle_matrix[i] = (PVFS_handle*)malloc(
	    HANDLE_BATCH*sizeof(PVFS_handle));
	if(!handle_matrix[i])
	{
	    perror("malloc");
	    return(-1);
	}
    }

    /* allocate some arrays to keep up with state */
    handle_count_array = (int*)malloc(server_count*sizeof(int));
    if(!handle_count_array)
    {
	perror("malloc");
	return(-1);
    }
    position_array = (PVFS_ds_position*)malloc(server_count*
	sizeof(PVFS_ds_position));
    if(!position_array)
    {
	perror("malloc");
	return(-1);
    }

    for(i=0; i<server_count; i++)
    {
	handle_count_array[i] = HANDLE_BATCH;
	position_array[i] = PVFS_ITERATE_START;
    }

    /* build a list of servers to talk to */
    addr_array = (PVFS_id_gen_t*)malloc(server_count*sizeof(PVFS_id_gen_t));
    if(!addr_array)
    {
	perror("malloc");
	return(-1);
    }
    ret = PVFS_mgmt_get_server_array(cur_fs, creds, 
	PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER,
	addr_array, &server_count);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return(-1);
    }

    /* put the servers into administrative mode */
    ret = PVFS_mgmt_setparam_list(
	cur_fs,
	creds,
	PVFS_SERV_PARAM_MODE,
	(int64_t)PVFS_SERVER_ADMIN_MODE,
	addr_array,
	NULL,
	server_count);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_setparam_list", ret);
	PVFS_mgmt_setparam_list(
	    cur_fs,
	    creds,
	    PVFS_SERV_PARAM_MODE,
	    (int64_t)PVFS_SERVER_NORMAL_MODE,
	    addr_array,
	    NULL,
	    server_count);
	return(-1);
    }

    /* iterate until we have retrieved all handles */
    do
    {
	ret = PVFS_mgmt_iterate_handles_list(
	    cur_fs,
	    creds,
	    handle_matrix,
	    handle_count_array,
	    position_array,
	    addr_array,
	    server_count);
	if(ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_iterate_handles_list", ret);
	    PVFS_mgmt_setparam_list(
		cur_fs,
		creds,
		PVFS_SERV_PARAM_MODE,
		(int64_t)PVFS_SERVER_NORMAL_MODE,
		addr_array,
		NULL,
		server_count);
	    return(-1);
	}

	for(i=0; i<server_count; i++)
	{
	    for(j=0; j<handle_count_array[i]; j++)
	    {
		printf("%s: 0x%08Lx\n", PVFS_mgmt_map_addr(cur_fs,
		    creds, addr_array[i], &tmp_type),
		    handle_matrix[i][j]);
	    }
	}

	/* find out if any servers have more handles to dump */
	more_flag = 0;
	for(i=0; i<server_count; i++)
	{
	    if(position_array[i] != PVFS_ITERATE_END)
	    {
		more_flag = 1;
		break;
	    }
	}
    }while(more_flag);

    PVFS_mgmt_setparam_list(
	cur_fs,
	creds,
	PVFS_SERV_PARAM_MODE,
	(int64_t)PVFS_SERVER_NORMAL_MODE,
	addr_array,
	NULL,
	server_count);

    PVFS_sys_finalize();

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

