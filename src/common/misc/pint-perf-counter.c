/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <string.h>
#include <sys/time.h>

#include "pvfs2-types.h"
#include "pint-perf-counter.h"
#include "gossip.h"
#include "gen-locks.h"

/* TODO: maybe this should just be malloc'd? */
/* TODO: maybe dimensions should be reversed?  look at which dimension is
 * looped over the most
 */
static int64_t perf_count_matrix[PINT_PERF_COUNT_KEY_MAX+1][PINT_PERF_HISTORY_SIZE];
static uint32_t perf_count_id[PINT_PERF_HISTORY_SIZE];
static uint64_t perf_count_start_times_ms[PINT_PERF_HISTORY_SIZE];
static int perf_count_head = 0;
static int perf_count_tail = 0;

static gen_mutex_t perf_mutex = GEN_MUTEX_INITIALIZER;

/* PINT_perf_initialize()
 *
 * initializes performance monitoring subsystem
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_perf_initialize(void)
{
    struct timeval tv;

    gen_mutex_lock(&perf_mutex);

    /* zero out counters */
    memset(perf_count_matrix, 0, 
	(PINT_PERF_COUNT_KEY_MAX+1)*PINT_PERF_HISTORY_SIZE*sizeof(int64_t));
    memset(perf_count_id, 0, PINT_PERF_HISTORY_SIZE*sizeof(uint32_t));
    memset(perf_count_start_times_ms, 0, 
	PINT_PERF_HISTORY_SIZE*sizeof(uint64_t));

    perf_count_head = 0;
    perf_count_tail = 0;

    gettimeofday(&tv, NULL);
    perf_count_start_times_ms[perf_count_head] = tv.tv_sec*1000 +
	tv.tv_usec/1000;

    gen_mutex_unlock(&perf_mutex);
    return(0);
}

/* PINT_perf_finalize()
 *
 * shuts down performance monitoring subsystem
 *
 * no return value
 */
void PINT_perf_finalize(void)
{
    return;
}

/* PINT_perf_count()
 *
 * record a simple single value metric.  key controls what category the
 * metric is for, op controls what should be done with the value (added, min,
 * max, etc)
 *
 * no return value
 */
void __PINT_perf_count(
    enum PINT_perf_count_keys key, 
    int64_t value,
    enum PINT_perf_ops op)
{
    gen_mutex_lock(&perf_mutex);
    switch(op)
    {
	case PINT_PERF_ADD:
            perf_count_matrix[key][perf_count_head] += value;
	    break;
	case PINT_PERF_SUB:
	    perf_count_matrix[key][perf_count_head] -= value;
	    break;
	case PINT_PERF_SET:
            perf_count_matrix[key][perf_count_head] = value;
	    break;
	default:
	    assert(0);
	    break;
    }
    gen_mutex_unlock(&perf_mutex);

    return;
}

static inline int64_t find_high_water_mark(int key)
{
    int64_t hwm = 0;
    int j = PINT_PERF_HISTORY_SIZE;

    while(--j > -1)
    {
        if (perf_count_matrix[key][j] > hwm)
        {
            hwm = perf_count_matrix[key][j];
        }
    }
    return hwm;
}

/* PINT_perf_rollover()
 *
 * triggers the next measurement interval, shifting or resetting any counters
 * as needed
 *
 * no return value
 */
void PINT_perf_rollover(void)
{
    int i = 0;
    uint32_t old_id;
    struct timeval tv;
    int64_t metadata_read_hwm = 0;
    int64_t metadata_write_hwm = 0;

    gen_mutex_lock(&perf_mutex);

    old_id = perf_count_id[perf_count_head];

    /* shift head of performance counters */
    perf_count_head = (perf_count_head+1)%PINT_PERF_HISTORY_SIZE;

    /* if we bump into the tail, shift it too */
    if(perf_count_tail == perf_count_head)
	perf_count_tail = (perf_count_tail+1)%PINT_PERF_HISTORY_SIZE;

    /* update metadata read/write high water marks */
    metadata_read_hwm = find_high_water_mark(PINT_PERF_METADATA_READ);
    metadata_write_hwm = find_high_water_mark(PINT_PERF_METADATA_WRITE);

    /* zero out everything in the current counter set */
    for(i = 0; i <= PINT_PERF_COUNT_KEY_MAX; i++)
    {
        /*
          but never zero metadata op counts; instead, set them all to
          the respective high water mark
        */
        if (i == PINT_PERF_METADATA_WRITE)
        {
            perf_count_matrix[i][perf_count_head] = metadata_write_hwm;
        }
        else if (i == PINT_PERF_METADATA_READ)
        {
            perf_count_matrix[i][perf_count_head] = metadata_read_hwm;
        }
        else
        {
            perf_count_matrix[i][perf_count_head] = 0;
        }
    }

    /* move to next id */
    perf_count_id[perf_count_head] = old_id + 1;

    /* set start time for next set */
    gettimeofday(&tv, NULL);
    perf_count_start_times_ms[perf_count_head] = 
	tv.tv_sec*1000 + tv.tv_usec/1000;

    gen_mutex_unlock(&perf_mutex);

    return;
}

/* PINT_perf_retrieve()
 *
 * fills in an array of performance statistics, beginning with next_id
 * (which is updated before the call returns).
 *
 * no return value
 */
void PINT_perf_retrieve(
    uint32_t* next_id,
    struct PVFS_mgmt_perf_stat* perf_array,
    int count,
    uint64_t* end_time_ms)
{
    int tmp_tail;
    int tmp_index = 0;

    gen_mutex_lock(&perf_mutex);

    tmp_tail = perf_count_tail;

    /* find out how to pick up at the lowest id not less that the requested 
     * next id
     */
    while(perf_count_id[tmp_tail] < *next_id && tmp_tail != perf_count_head)
    {
	tmp_tail = (tmp_tail+1)%PINT_PERF_HISTORY_SIZE;
    }

    /* fill in as many valid perf stat entries as we can */
    while(tmp_tail != perf_count_head && tmp_index < count)
    {
	perf_array[tmp_index].valid_flag = 1;
	perf_array[tmp_index].id = perf_count_id[tmp_tail];
	perf_array[tmp_index].start_time_ms 
	    = perf_count_start_times_ms[tmp_tail];
	perf_array[tmp_index].write
	    = perf_count_matrix[PINT_PERF_WRITE][tmp_tail];
	perf_array[tmp_index].read
	    = perf_count_matrix[PINT_PERF_READ][tmp_tail];
	perf_array[tmp_index].metadata_write
	    = perf_count_matrix[PINT_PERF_METADATA_WRITE][tmp_tail];
	perf_array[tmp_index].metadata_read
	    = perf_count_matrix[PINT_PERF_METADATA_READ][tmp_tail];

	tmp_tail = (tmp_tail+1)%PINT_PERF_HISTORY_SIZE;
	tmp_index++;
    }

    /* mark the rest as invalid */
    while(tmp_index < count)
    {
	perf_array[tmp_index].valid_flag = 0;
	tmp_index++;
    }

    /* update next_id to what the client should ask for next */
    *next_id = perf_count_id[tmp_tail];
    *end_time_ms = perf_count_start_times_ms[tmp_tail];

    gen_mutex_unlock(&perf_mutex);

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
