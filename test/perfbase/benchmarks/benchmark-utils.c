
#include "benchmark-utils.h"
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <string.h>
#include <stdio.h>
#include <math.h>

static struct PVFS_mgmt_perf_stat ** perf_matrix;
static uint32_t * next_id_array;
static uint64_t * end_time_ms_array;
static PVFS_BMI_addr_t * addr_array;

#define HISTORY 5

int test_util_init_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credentials creds,
    int32_t flags,
    int * server_count)
{
    int ret, i;
    int count;

    /* count how many meta servers we have */
    ret = PVFS_mgmt_count_servers(cur_fs, &creds, flags,
				  server_count);
    if(ret < 0)
    {
	PVFS_perror("PVFS_mgmt_count_servers", ret);
	return(ret);
    }

    count = *server_count;
    /* allocate a 2 dimensional array for statistics */
    perf_matrix = (struct PVFS_mgmt_perf_stat**)malloc(
	count*sizeof(struct PVFS_mgmt_perf_stat*));
    if(!perf_matrix)
    {
	PVFS_perror("malloc", -1);
	return(-1);
    }
    for(i=0; i<count; i++)
    {
	perf_matrix[i] = (struct PVFS_mgmt_perf_stat *)
	    malloc(HISTORY * sizeof(struct PVFS_mgmt_perf_stat));
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
				     &creds,
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
    PVFS_credentials creds,
    int count)
{
    int ret, i, j;
    ret = PVFS_mgmt_perf_mon_list(
	cur_fs, &creds, perf_matrix, 
	end_time_ms_array, addr_array, next_id_array,
	count, HISTORY, NULL);
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

	    printf("%d\t%ull\t%ull\t%ull\n",
		   count,
		   perf_matrix[i][j].start_time_ms,
		   perf_matrix[i][j].write,
		   perf_matrix[i][j].read);
	}
    }

    return 0;
}


int test_util_get_metadata_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credentials creds,
    int count)
{
    int ret, i, j;
    ret = PVFS_mgmt_perf_mon_list(
	cur_fs, &creds, perf_matrix, 
	end_time_ms_array, addr_array, next_id_array,
	count, HISTORY, NULL);
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

	    printf("%d\t%ull\t%ull\t%ull\n",
		   count,
		   perf_matrix[i][j].start_time_ms,
		   perf_matrix[i][j].metadata_write,
		   perf_matrix[i][j].metadata_read);
	}
    }

    return 0;
}

int test_util_get_queue_perfs(
    PVFS_fs_id cur_fs,
    PVFS_credentials creds,
    int count)
{
    int ret, i, j;
    ret = PVFS_mgmt_perf_mon_list(
	cur_fs, &creds, perf_matrix, 
	end_time_ms_array, addr_array, next_id_array,
	count, HISTORY, NULL);
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

	    printf("%d\t%ull\t%u\t%u\t%u\n",
		   count,
		   perf_matrix[i][j].start_time_ms,
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
