/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-event.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

#define EVENT_DEPTH 2000

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
    struct options* user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    int i,j;
    PVFS_credentials creds;
    int io_server_count;
    struct PVFS_mgmt_event** event_matrix;
    PVFS_BMI_addr_t *addr_array;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
	fprintf(stderr, "Error: failed to parse command line arguments.\n");
	usage(argc, argv);
	return -1;
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
	PVFS_perror("PVFS_util_resolve", ret);
	return -1;
    }

    PVFS_util_gen_credentials(&creds);

    /* count how many I/O servers we have */
    ret = PVFS_mgmt_count_servers(cur_fs,
				  &creds,
				  PVFS_MGMT_IO_SERVER,
				  &io_server_count);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_count_servers", ret);
	return -1;
    }

    /* allocate a 2 dimensional array for events */
    event_matrix = (struct PVFS_mgmt_event **)
	malloc(io_server_count * sizeof(struct PVFS_mgmt_event *));
    if (event_matrix == NULL)
    {
	perror("malloc");
	return -1;
    }

    for (i=0; i < io_server_count; i++)
    {
	event_matrix[i] = (struct PVFS_mgmt_event *)
	    malloc(EVENT_DEPTH * sizeof(struct PVFS_mgmt_event));
	if (event_matrix[i] == NULL)
	{
	    perror("malloc");
	    return -1;
	}
    }

    /* build a list of servers to talk to */
    addr_array = (PVFS_BMI_addr_t *)
	malloc(io_server_count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
	perror("malloc");
	return -1;
    }
    ret = PVFS_mgmt_get_server_array(cur_fs,
				     &creds,
				     PVFS_MGMT_IO_SERVER,
				     addr_array,
				     &io_server_count);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return -1;
    }

    /* grap current events */
    ret = PVFS_mgmt_event_mon_list(cur_fs,
				   &creds,
				   event_matrix,
				   addr_array, 
				   io_server_count,
				   EVENT_DEPTH,
				   NULL /* detailed errors */);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_event_mon_list", EVENT_DEPTH);
	return -1;
    }

    printf("# (server number) (api) (operation) (value) (id) (flags) (sec) (usec)\n");
    for (i=0; i < io_server_count; i++)
    {
	for (j=0; j < EVENT_DEPTH; j++)
	{
	    if ((event_matrix[i][j].flags & PVFS_EVENT_FLAG_INVALID) == 0)
	    {
		printf("%d %d %d %Ld %Ld %d %d %d\n", 
		       i, 
		       (int)event_matrix[i][j].api,
		       (int)event_matrix[i][j].operation,
		       (long long)event_matrix[i][j].value,
		       (long long)event_matrix[i][j].id,
		       (int)event_matrix[i][j].flags,
		       (int)event_matrix[i][j].tv_sec,
		       (int)event_matrix[i][j].tv_usec);
	    }
	}
    }

    PVFS_sys_finalize();

    return ret;
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    char flags[] = "vm:";
    int one_opt = 0;
    int len = 0;

    struct options *tmp_opts = NULL;
    int ret = -1;

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if (tmp_opts == NULL)
    {
	return NULL;
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
		tmp_opts->mnt_point = (char *) malloc(len + 1);
		if (tmp_opts->mnt_point == NULL)
		{
		    free(tmp_opts);
		    return NULL;
		}
		memset(tmp_opts->mnt_point, 0, len+1);
		ret = sscanf(optarg, "%s", tmp_opts->mnt_point);
		if(ret < 1){
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
		exit(EXIT_FAILURE);
	}
    }

    if (!tmp_opts->mnt_point_set)
    {
	free(tmp_opts);
	return NULL;
    }

    return tmp_opts;
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

