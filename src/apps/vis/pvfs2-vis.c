/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>

#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-vis.h"

#define HISTORY 5

struct poll_thread_args
{
    PVFS_fs_id fs;
    PVFS_credentials credentials;
    PVFS_BMI_addr_t *addr_array;
    struct PVFS_mgmt_perf_stat **tmp_matrix;
    uint32_t *next_id_array;
    uint64_t *end_time_ms_array;
    int server_count;
    int history_count;
    struct timespec req;
};

struct pvfs2_vis_buffer pint_vis_shared;
pthread_mutex_t pint_vis_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pint_vis_cond = PTHREAD_COND_INITIALIZER;
int pint_vis_error = 0;

static void* poll_for_updates(void* args);
static pthread_t poll_thread_id = -1;

/* pvfs2_vis_stop()
 *
 * shuts down currently running pvfs2 vis thread
 *
 * returns 0 on success, -PVFS_error on failure
 */
int pvfs2_vis_stop(void)
{
    if(poll_thread_id > 0)
    {
	pthread_cancel(poll_thread_id);
    }

    PVFS_sys_finalize();

    return(0);
}

/* pvfs2_vis_start()
 *
 * gathers statistics from the servers associated with given path (using
 * a thread in the background)
 *
 * returns 0 on success, -PVFS_error on failure
 */
int pvfs2_vis_start(char* path, int update_interval)
{
    PVFS_fs_id cur_fs;
    PVFS_util_tab mnt = {0,NULL};
    char pvfs_path[PVFS_NAME_MAX] = {0};
    int i,j;
    int mnt_index = -1;
    PVFS_sysresp_init resp_init;
    PVFS_credentials creds;
    int ret = -1;
    int io_server_count = 0;
    uint32_t* next_id_array;
    struct PVFS_mgmt_perf_stat** perf_matrix;
    uint64_t* end_time_ms_array;
    PVFS_BMI_addr_t *addr_array;
    int done = 0;
    struct poll_thread_args* args;
    struct timespec req;

    req.tv_sec = update_interval/1000;
    req.tv_nsec = (update_interval%1000)*1000*1000;

    /* allocate storage to convey information to thread */
    args = (struct poll_thread_args*)malloc(sizeof(struct poll_thread_args));
    if(!args)
    {
	return(-PVFS_ENOMEM);
    }

    /* look at pvfstab */
    ret = PVFS_util_parse_pvfstab(NULL, &mnt);
    if(ret < 0)
    {
        return(ret);
    }

    /* see if the destination resides on any of the file systems
     * listed in the pvfstab; find the pvfs fs relative path
     */
    for(i=0; i<mnt.mntent_count; i++)
    {
	ret = PVFS_util_remove_dir_prefix(path,
	    mnt.mntent_array[i].mnt_dir, pvfs_path, PVFS_NAME_MAX);
	if(ret == 0)
	{
	    mnt_index = i;
	    break;
	}
    }

    if(mnt_index == -1)
    {
	return(-PVFS_ENOENT);
    }

    memset(&resp_init, 0, sizeof(resp_init));
    ret = PVFS_sys_initialize(mnt, 0, &resp_init);
    if(ret < 0)
    {
	return(ret);
    }

    cur_fs = resp_init.fsid_list[mnt_index];

    creds.uid = getuid();
    creds.gid = getgid();

    /* count how many I/O servers we have */
    ret = PVFS_mgmt_count_servers(cur_fs, creds, PVFS_MGMT_IO_SERVER,
	&io_server_count);
    if(ret < 0)
    {
	return(ret);
    }

    /* allocate a 2 dimensional array for statistics */
    perf_matrix = (struct PVFS_mgmt_perf_stat**)malloc(
	io_server_count*sizeof(struct PVFS_mgmt_perf_stat*));
    if(!perf_matrix)
    {
	return(-PVFS_ENOMEM);
    }
    for(i=0; i<io_server_count; i++)
    {
	perf_matrix[i] = (struct PVFS_mgmt_perf_stat*)malloc(
	    HISTORY*sizeof(struct PVFS_mgmt_perf_stat));
	if(!perf_matrix[i])
	{
	    return(-PVFS_ENOMEM);
	}
    }

    /* allocate an array to keep up with what iteration of statistics
     * we need from each server 
     */
    next_id_array = (uint32_t*)malloc(io_server_count*sizeof(uint32_t));
    if (next_id_array == NULL)
    {
	return -PVFS_ENOMEM;
    }
    memset(next_id_array, 0, io_server_count * sizeof(uint32_t));

    end_time_ms_array = (uint64_t *) malloc(io_server_count*sizeof(uint64_t));
    if (end_time_ms_array == NULL)
    {
	return -PVFS_ENOMEM;
    }

    /* build a list of servers to talk to */
    addr_array = (PVFS_BMI_addr_t *)
	malloc(io_server_count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
	return -PVFS_ENOMEM;
    }
    ret = PVFS_mgmt_get_server_array(cur_fs,
				     creds,
				     PVFS_MGMT_IO_SERVER,
				     addr_array,
				     &io_server_count);
    if (ret < 0)
    {
	return ret;
    }

    /* loop for a little bit, until we have 5 measurements queued up from each
     * server
     */
    while (!done)
    {
	memset(next_id_array, 0, io_server_count*sizeof(uint32_t));
	ret = PVFS_mgmt_perf_mon_list(cur_fs,
				      creds,
				      perf_matrix, 
				      end_time_ms_array,
				      addr_array,
				      next_id_array,
				      io_server_count, 
				      HISTORY);
	if (ret < 0)
	{
	    return ret;
	}

	done = 1;
	for (i=0; i < io_server_count; i++)
	{
	    for (j=0; j < HISTORY; j++)
	    {
		if (!perf_matrix[i][j].valid_flag)
		    done = 0;
	    }
	}
	nanosleep(&req, NULL);
    }

    /* populate the shared performance data */
    pint_vis_shared.io_count = io_server_count;
    pint_vis_shared.io_depth = HISTORY;

    /* allocate a 2 dimensional array for statistics */
    pint_vis_shared.io_perf_matrix = (struct PVFS_mgmt_perf_stat**)malloc(
	io_server_count*sizeof(struct PVFS_mgmt_perf_stat*));
    if (pint_vis_shared.io_perf_matrix == NULL)
    {
	return -PVFS_ENOMEM;
    }
    for (i=0; i < io_server_count; i++)
    {
	pint_vis_shared.io_perf_matrix[i] = (struct PVFS_mgmt_perf_stat *)
	    malloc(HISTORY * sizeof(struct PVFS_mgmt_perf_stat));
	if (pint_vis_shared.io_perf_matrix[i] == NULL)
	{
	    return -PVFS_ENOMEM;
	}
    }

    pint_vis_shared.io_end_time_ms_array = (uint64_t *)
	malloc(io_server_count * sizeof(uint64_t));
    if (pint_vis_shared.io_end_time_ms_array == NULL)
    {
	return -PVFS_ENOMEM;
    }

    /* fill in first statistics */
    for(i=0; i<io_server_count; i++)
    {
	memcpy(pint_vis_shared.io_perf_matrix[i],
	       perf_matrix[i],
	       HISTORY * sizeof(struct PVFS_mgmt_perf_stat));
    }
    memcpy(pint_vis_shared.io_end_time_ms_array,
	   end_time_ms_array,
	   io_server_count * sizeof(uint64_t));

    /* setup arguments to pass to monitoring thread */
    args->fs = cur_fs;
    args->credentials = creds;
    args->addr_array = addr_array;
    args->tmp_matrix = perf_matrix;
    args->next_id_array = next_id_array;
    args->end_time_ms_array = end_time_ms_array;
    args->server_count = io_server_count;
    args->history_count = HISTORY;
    args->req = req;

    /* launch thread */
    ret = pthread_create(&poll_thread_id, NULL, poll_for_updates, args);
    if (ret != 0)
    {
	return ret;
    }

    return 0;
}

/* poll_for_updates()
 *
 * sends a periodic request to the servers to probe for new performance 
 * statistics, shifting them into the shared perf data as needed
 *
 * NOTE: we only pass in the tmp_matrix for convenience to avoid having
 * to allocate another matrix; the caller already has some buffers we can use
 *
 * returns NULL, setting pint_vis_error if an error occurred
 */
static void *poll_for_updates(void *args)
{
    struct poll_thread_args* tmp_args = (struct poll_thread_args*)args;
    int ret;
    int i, j, k;
    int new_count;
    int new_flag = 0;
    PVFS_fs_id fs = tmp_args->fs;
    PVFS_credentials credentials = tmp_args->credentials;
    PVFS_BMI_addr_t *addr_array = tmp_args->addr_array;
    struct PVFS_mgmt_perf_stat** tmp_matrix = tmp_args->tmp_matrix;
    uint32_t* next_id_array = tmp_args->next_id_array;
    uint64_t* end_time_ms_array = tmp_args->end_time_ms_array;
    int server_count = tmp_args->server_count;
    int history_count = tmp_args->history_count;
    struct timespec req = tmp_args->req;

    while (1)
    {
	ret = PVFS_mgmt_perf_mon_list(fs,
				      credentials,
				      tmp_matrix, 
				      end_time_ms_array,
				      addr_array,
				      next_id_array,
				      server_count, 
				      history_count);
	if (ret < 0)
	{
	    pint_vis_error = ret;
	    poll_thread_id = -1;
	    PVFS_perror_gossip("PVFS_mgmt_perf_mon_list", ret);
	    pthread_cond_signal(&pint_vis_cond);
	    pthread_mutex_unlock(&pint_vis_mutex);

	    return NULL;
	}

	new_flag = 0;

	pthread_mutex_lock(&pint_vis_mutex);
	for (i=0; i < server_count; i++)
	{
	    new_count = 0;
	    for (j=0; j < history_count; j++)
	    {
		if (tmp_matrix[i][j].valid_flag)
		{
		    new_count++;
		    new_flag = 1;
		}
	    }
	    if (new_count > 0)
	    {
		/* if we hit this point, we need to shift one or more
		 * new measurements into position
		 */
		for (k=new_count; k < history_count; k++)
		{
		    /* move old ones over */
		    pint_vis_shared.io_perf_matrix[i][k-new_count] =
			pint_vis_shared.io_perf_matrix[i][k];
		}
		for (k=(history_count-new_count); k<history_count; k++)
		{
		    /* drop new ones in */
		    pint_vis_shared.io_perf_matrix[i][k] = 
			tmp_matrix[i][k-(history_count-new_count)];
		}
		/* update end time */
		pint_vis_shared.io_end_time_ms_array[i] =
		    end_time_ms_array[i];

	    }
	}

	if (new_flag)
	{
	    pthread_cond_signal(&pint_vis_cond);
	}
	pthread_mutex_unlock(&pint_vis_mutex);

	nanosleep(&req, NULL);
    }

    return NULL;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
