
#include "benchmark-utils.h"
#include "pvfs2-internal.h"
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <string.h>
#include <stdio.h>
#include <math.h>

/* this struct is now only used by karma */
/* perf numbers are returned as an array of int64_t */

struct PVFS_mgmt_perf_stat
{
    int32_t valid_flag;
    int64_t start_time_ms;
    int64_t read;
    int64_t write;
    int64_t metadata_read;
    int64_t metadata_write;
};

static int64_t ** perf_matrix;
static uint32_t * next_id_array;
static uint64_t * end_time_ms_array;
static PVFS_BMI_addr_t * addr_array;

#define HISTORY 5

int test_util_init_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credential creds,
    int32_t flags,
    int * server_count)
{
    int ret, i;
    int count;

    /* count how many meta servers we have */
    ret = PVFS_mgmt_count_servers(cur_fs, flags,
				  server_count);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_count_servers", ret);
	return(ret);
    }

    count = *server_count;
    /* allocate a 2 dimensional array for statistics */
    perf_matrix = (int64_t **)malloc(
	count*sizeof(int64_t*));
    if(!perf_matrix)
    {
	PVFS_perror("malloc", -1);
	return(-1);
    }
    for(i=0; i<count; i++)
    {
	perf_matrix[i] = (int64_t *)
	    malloc(HISTORY * sizeof(int64_t));
	if (perf_matrix[i] == NULL)
	{
	    PVFS_perror("malloc", -1);
	    return -1;
	}
    }

    /* allocate an array to keep up with what iteration of statistics
     * we need from each server 
     */
    next_id_array = (uint32_t *) malloc(count * sizeof(uint32_t));
    if (next_id_array == NULL)
    {
	PVFS_perror("malloc", -1);
	return -1;
    }
    memset(next_id_array, 0, count*sizeof(uint32_t));

    /* allocate an array to keep up with end times from each server */
    end_time_ms_array = (uint64_t *)
	malloc(count * sizeof(uint64_t));
    if (end_time_ms_array == NULL)
    {
	PVFS_perror("malloc", -1);
	return -1;
    }

    /* build a list of servers to talk to */
    addr_array = (PVFS_BMI_addr_t *)
	malloc(count * sizeof(PVFS_BMI_addr_t));
    if (addr_array == NULL)
    {
	PVFS_perror("malloc", -1);
	return -1;
    }
    ret = PVFS_mgmt_get_server_array(cur_fs,
				     flags,
				     addr_array,
				     &count);
    if (ret < 0)
    {
	PVFS_perror("PVFS_mgmt_get_server_array", ret);
	return -1;
    }

    return 0;

}

int test_util_get_io_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credential creds,
    int count)
{
    int ret, i, j;
    ret = PVFS_mgmt_perf_mon_list(
    		cur_fs,
    		&creds,
    		perf_matrix,
    		end_time_ms_array,
    		addr_array,
    		next_id_array,
    		count,
    		HISTORY,
    		NULL,
    		NULL);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	return -1;
    }

    for(i = 0; i < count; i++)
    {
	for(j = 0; j < HISTORY; ++j)
	{
	    printf("%d\t%lld\n",
		   count,
		   lld(perf_matrix[i][j]));
    }
    }
    return 0;
}


int test_util_get_metadata_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credential creds,
    int count)
{
    int ret, i, j;
    ret = PVFS_mgmt_perf_mon_list(
	cur_fs, &creds, perf_matrix, 
	end_time_ms_array, addr_array, next_id_array,
	count, HISTORY, NULL, NULL);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	return -1;
    }

    for(i = 0; i < count; i++)
    {
	for(j = 0; j < HISTORY; ++j)
	{
	    if(!perf_matrix[i][j].valid_flag)
	    {
		break;
	    }

	    printf("%d\t%llu\t%lld\t%lld\n",
		   count,
		   llu(perf_matrix[i][j].start_time_ms),
		   lld(perf_matrix[i][j].metadata_write),
		   lld(perf_matrix[i][j].metadata_read));
	}
    }

    return 0;
}

int test_util_get_queue_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credential creds,
    int count)
{
    int ret, i, j;
    ret = PVFS_mgmt_perf_mon_list(
	cur_fs, &creds, perf_matrix, 
	end_time_ms_array, addr_array, next_id_array,
	count, HISTORY, NULL, NULL);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_perf_mon_list", ret);
	return -1;
    }

    for(i = 0; i < count; i++)
    {
	for(j = 0; j < HISTORY; ++j)
	{
	    if(!perf_matrix[i][j].valid_flag)
	    {
		break;
	    }

	    printf("%d\t%llu\t%u\t%u\t%u\n",
		   count,
		   llu(perf_matrix[i][j].start_time_ms),
		   perf_matrix[i][j].dspace_queue,
		   perf_matrix[i][j].keyval_queue,
		   perf_matrix[i][j].reqsched);
	}
    }

    return 0;
}

double results[10000];
int results_count = 0;
double result_sum = 0;
struct timeval start;

void test_util_start_timing(void)
{
    gettimeofday(&start, NULL);
}


    
void test_util_stop_timing(void)
{
    double result;
    struct timeval stop;
    gettimeofday(&stop, NULL);

    result = (stop.tv_sec + stop.tv_usec * 1e-6) - 
	     (start.tv_sec + start.tv_usec * 1e-6);

    results[results_count++] = result;
    result_sum += result;
}

void test_util_print_timing(int rank)
{
    printf("%d\t%d\t%f\n", rank, results_count, results[results_count-1]);
}


void test_util_print_avg_and_dev(void)
{
    int i = 0;
    float avg, dev;

    avg = ((float)result_sum) / results_count;

    dev = 0;
    for(i = 0; i < results_count; ++i)
    {
	dev += (results[i] - avg) * (results[i] - avg);
    }
    dev = sqrt(dev / results_count);
    
    printf("%d\t%f\t%f\n", results_count, avg, dev);
}
