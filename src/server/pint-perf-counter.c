/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <string.h>

#include "pvfs2-types.h"
#include "pint-perf-counter.h"
#include "gossip.h"

/* TODO: maybe this should just be malloc'd? */
/* TODO: maybe dimensions should be reversed?  look at which dimension is
 * looped over the most
 */
static int64_t perf_count_matrix[PINT_PERF_COUNT_KEY_MAX+1][PINT_PERF_HISTORY_SIZE];
static int perf_count_head = 0;
static int perf_count_tail = 0;

/* PINT_perf_initialize()
 *
 * initializes performance monitoring subsystem
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_perf_initialize(void)
{
    /* zero out counters */
    memset(perf_count_matrix, 0, 
	(PINT_PERF_COUNT_KEY_MAX+1)*PINT_PERF_HISTORY_SIZE*sizeof(int64_t));

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
void PINT_perf_count(enum PINT_perf_count_keys key, 
    int64_t value,
    enum PINT_perf_ops op)
{
    assert(key > -1 && key <= PINT_PERF_COUNT_KEY_MAX);
    
    switch(op)
    {
	case PINT_PERF_ADD:
	    perf_count_matrix[key][perf_count_head] += value;
	    break;
	case PINT_PERF_SUB:
	    perf_count_matrix[key][perf_count_head] -= value;
	    break;
	default:
	    assert(0);
	    break;
    }

    return;
}

/* PINT_perf_rollover()
 *
 * triggers the next measurement interval, shifting or resetting any counters
 * as needed
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_perf_rollover(void)
{
    int i;

    /* shift head of performance counters */
    perf_count_head = (perf_count_head+1)%PINT_PERF_HISTORY_SIZE;

    /* if we bump into the tail, shift it too */
    if(perf_count_tail == perf_count_head)
	perf_count_tail = (perf_count_tail+1)%PINT_PERF_HISTORY_SIZE;

    /* zero out everything in the current counter set */
    for(i=0; i<PINT_PERF_COUNT_KEY_MAX; i++)
    {
	perf_count_matrix[i][perf_count_head] = 0;
    }

    /* TODO: keep track of an identifier for client to use when polling
     * for values
     */

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
