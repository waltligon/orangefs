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
#include "pint-sysint-utils.h"
#include "server-config.h"

#ifndef PVFS2_VERSION
#define PVFS2_VERSION "Unknown"
#endif

struct options
{
    char* mnt_point;
    int mnt_point_set;
    int human_readable;
    int use_si_units;
};

static struct options *parse_args(int argc, char *argv[]);
static void usage(int argc, char **argv);

#define SCRATCH_LEN 16

int main(int argc, char **argv)
{
    int ret = -1;
    PVFS_fs_id cur_fs;
    struct options *user_opts = NULL;
    char pvfs_path[PVFS_NAME_MAX] = {0};
    PVFS_sysresp_statfs resp_statfs;
    int i,j;
    PVFS_credentials creds;
    struct PVFS_mgmt_server_stat *stat_array = NULL;
    int outcount;
    PVFS_BMI_addr_t *addr_array;
    int server_type;
    char scratch_size[SCRATCH_LEN] = {0};
    char scratch_total[SCRATCH_LEN] = {0};

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

    /* gather normal statfs statistics from system interface */
    ret = PVFS_sys_statfs(cur_fs, &creds, &resp_statfs);
    if(ret < 0)
    {
	PVFS_perror("PVFS_sys_statfs", ret);
	return(-1);
    }

    printf("\naggregate statistics:\n");
    printf("---------------------------------------\n\n");
    printf("\tfs_id: %d\n", (int)resp_statfs.statfs_buf.fs_id);
    printf("\ttotal number of servers (meta and I/O): %d\n", 
	resp_statfs.server_count);
    printf("\thandles available (meta and I/O):       %Lu\n",
	(long long unsigned)resp_statfs.statfs_buf.handles_available_count);
    printf("\thandles total (meta and I/O):           %Lu\n",
	(long long unsigned)resp_statfs.statfs_buf.handles_total_count);
    if (user_opts->human_readable) {
	PVFS_util_make_size_human_readable(
		(long long)resp_statfs.statfs_buf.bytes_available,
		scratch_size, SCRATCH_LEN, user_opts->use_si_units);
	PVFS_util_make_size_human_readable(
		(long long)resp_statfs.statfs_buf.bytes_total,
		scratch_total, SCRATCH_LEN, user_opts->use_si_units);
	printf("\tbytes available:                        %s\n", scratch_size);
	printf("\tbytes total:                            %s\n", scratch_size); 
    } else {
	printf("\tbytes available:                        %Ld\n", 
	    (long long)resp_statfs.statfs_buf.bytes_available);
	printf("\tbytes total:                            %Ld\n", 
	    (long long)resp_statfs.statfs_buf.bytes_total);
    }
    printf("\nNOTE: The aggregate total and available statistics are calculated based\n");
    printf("on an algorithm that assumes data will be distributed evenly; thus\n");
    printf("the free space is equal to the smallest I/O server capacity\n");
    printf("multiplied by the number of I/O servers.  If this number seems\n");
    printf("unusually small, then check the individual server statistics below\n");
    printf("to look for problematic servers.\n");

    /* now call management functions to determine per-server statistics */
    stat_array = (struct PVFS_mgmt_server_stat *)
	malloc(resp_statfs.server_count* sizeof(struct PVFS_mgmt_server_stat));
    if (stat_array == NULL)
    {
	perror("malloc");
	return -1;
    }

    for (j=0; j<2; j++)
    {
	if (j==0)
	{
	    server_type = PVFS_MGMT_META_SERVER;
	}
	else
	{
	    server_type = PVFS_MGMT_IO_SERVER;
	}

	ret = PVFS_mgmt_count_servers(
            cur_fs, &creds, server_type, &outcount);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_count_servers", ret);
	    return -1;
	}

	addr_array = (PVFS_BMI_addr_t *)
	    malloc(outcount * sizeof(PVFS_BMI_addr_t));
	if (addr_array == NULL)
	{
	    perror("malloc");
	    return -1;
	}

	ret = PVFS_mgmt_get_server_array(
            cur_fs, &creds, server_type, addr_array, &outcount);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_get_server_array", ret);
	    return -1;
	}

	ret = PVFS_mgmt_statfs_list(
            cur_fs, &creds, stat_array, addr_array, outcount, NULL);
	if (ret < 0)
	{
	    PVFS_perror("PVFS_mgmt_statfs_list", ret);
	    return -1;
	}

	if (j==0)
	{
	    printf("\nmeta server statistics:\n");
	}
	else
	{
	    printf("\nI/O server statistics:\n");
	}

	printf("---------------------------------------\n\n");

	for(i=0; i < outcount; i++)
	{
	    printf("server: %s\n", stat_array[i].bmi_address);

	    if (user_opts->human_readable)
            {
		PVFS_util_make_size_human_readable(
			(long long)stat_array[i].ram_total_bytes,
			scratch_size, SCRATCH_LEN,
                        user_opts->use_si_units);
		PVFS_util_make_size_human_readable(
			(long long)stat_array[i].ram_free_bytes,
			scratch_total, SCRATCH_LEN,
                        user_opts->use_si_units);
		printf("\tRAM total        : %s\n", scratch_size);
		printf("\tRAM free         : %s\n", scratch_total);
                printf("\tuptime           : %d hours, %.2d minutes\n",
                       (int)((stat_array[i].uptime_seconds / 60) / 60),
                       (int)((stat_array[i].uptime_seconds / 60) % 60));
	    }
            else
            {
                printf("\tRAM bytes total  : %Lu\n",
                       Lu(stat_array[i].ram_total_bytes));
                printf("\tRAM bytes free   : %Lu\n",
                       Lu(stat_array[i].ram_free_bytes));
                printf("\tuptime (seconds) : %Lu\n",
                       Lu(stat_array[i].uptime_seconds));
            }
	    printf("\thandles available: %Lu\n",
                   Lu(stat_array[i].handles_available_count));
	    printf("\thandles total    : %Lu\n",
                   Lu(stat_array[i].handles_total_count));

	    if (user_opts->human_readable)
            {
		PVFS_util_make_size_human_readable(
			(long long)stat_array[i].bytes_available,
			scratch_size, SCRATCH_LEN,
                        user_opts->use_si_units);
		PVFS_util_make_size_human_readable(
			(long long)stat_array[i].bytes_total,
			scratch_total, SCRATCH_LEN,
                        user_opts->use_si_units);
		printf("\tbytes available  : %s\n", scratch_size);
		printf("\tbytes total      : %s\n", scratch_total);
	    }
            else
            {
		printf("\tbytes available  : %Ld\n",
                       Ld(stat_array[i].bytes_available));
		printf("\tbytes total      : %Ld\n",
                       Ld(stat_array[i].bytes_total));
	    }

	    if (stat_array[i].server_type &
                (PVFS_MGMT_IO_SERVER|PVFS_MGMT_META_SERVER))
            {
		printf("\tmode: serving both metadata and I/O data\n");
            }
	    else if(stat_array[i].server_type & PVFS_MGMT_IO_SERVER)
            {
		printf("\tmode: serving only I/O data\n");
            }
	    else if(stat_array[i].server_type & PVFS_MGMT_META_SERVER)
            {
		printf("\tmode: serving only metadata\n");
            }
	    printf("\n");
	}
	free(addr_array);
    }

    free(stat_array);
    return(ret);
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char *argv[])
{
    /* getopt stuff */
    extern char *optarg;
    extern int optind, opterr, optopt;
    char flags[] = "hHvm:";
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
    while ((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch (one_opt)
        {
            case('v'):
                printf("%s\n", PVFS2_VERSION);
                exit(0);
	    case('h'):
		tmp_opts->human_readable = 1;
		break;
	    case('H'):
		tmp_opts->human_readable = 1;
		tmp_opts->use_si_units = 1;
		break;
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
		if (ret < 1)
		{
		    free(tmp_opts->mnt_point);
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

    if(!tmp_opts->mnt_point_set)
    {
	free(tmp_opts);
	return NULL;
    }

    return tmp_opts;
}


static void usage(int argc, char** argv)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage  : %s [-m fs_mount_point] [-h,-H]\n", argv[0]);
    fprintf(stderr, "Example: %s -m /mnt/pvfs2\n", argv[0]);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

