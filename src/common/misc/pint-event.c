/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <sys/time.h>

#include "pint-event.h"
#include "pvfs2-types.h"
#include "gossip.h"

/* variables that provide runtime control over which events are recorded */
int PINT_event_on = 0;
int32_t PINT_event_api_mask = 0;
int32_t PINT_event_op_mask = 0;

/* represents an individual measurement */
struct PINT_ts_datapoint
{
    int32_t api;
    int32_t operation;
    int64_t value;
    PVFS_id_gen_t id;
    int8_t flags;
    int64_t tv_sec;
    int64_t tv_usec;
};

/* global data structures for storing measurements */
static struct PINT_ts_datapoint* ts_ring = NULL;
static int ts_head = 0;
static int ts_tail = 0;
static int ts_ring_size = 0;

/* PINT_event_initialize()
 *
 * starts up the event logging interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_event_initialize(int ring_size)
{
    if(ts_ring != NULL)
	return(-PVFS_EALREADY);

    /* give a reasonable ring buffer size to work with! */
    if(ring_size < 4)
	return(-PVFS_EINVAL);

    /* allocate a ring buffer for time stamped events */
    ts_ring = (struct PINT_ts_datapoint*)malloc(ring_size
	*sizeof(struct PINT_ts_datapoint));
    if(!ts_ring)
	return(-PVFS_ENOMEM);

    ts_head = 0;
    ts_tail = 0;
    ts_ring_size = ring_size;

    return(0);
}

/* PINT_event_finalize()
 *
 * shuts down the event logging interface 
 *
 * returns 0 on success, -PVFS_error on failure
 */
void PINT_event_finalize(void)
{
    
    if(ts_ring == NULL)
	return;

    free(ts_ring);
    ts_ring = NULL;
    ts_head = 0;
    ts_tail = 0;
    ts_ring_size = 0;

    return;
}

/* PINT_event_set_masks()
 *
 * sets masks that determine if event logging is enabled, what the api mask
 * is, and what the operation mask is.  The combination of these values
 * determines which events are recorded
 *
 * no return value
 */
void PINT_event_set_masks(int event_on, int32_t api_mask, int32_t op_mask)
{
    PINT_event_on = event_on;
    PINT_event_api_mask = api_mask;
    PINT_event_op_mask = op_mask;

    return;
}


/* PINT_event_get_masks()
 *
 * retrieves current mask values 
 *
 * no return value
 */
void PINT_event_get_masks(int* event_on, int32_t* api_mask, int32_t* op_mask)
{
    *event_on = PINT_event_on;
    *api_mask = PINT_event_api_mask;
    *op_mask = PINT_event_op_mask;

    return;
}

/* PINT_event_timestamp()
 *
 * records a timestamp in the ring buffer
 *
 * returns 0 on success, -PVFS_error on failure
 */
void __PINT_event_timestamp(
    enum PINT_event_api api,
    int32_t operation,
    int64_t value,
    PVFS_id_gen_t id,
    int8_t flags)
{
    struct timeval tv;

    /* immediately grab timestamp */
    gettimeofday(&tv, NULL);

    /* fill in event */
    ts_ring[ts_head].api = api;
    ts_ring[ts_head].operation = operation;
    ts_ring[ts_head].value = value;
    ts_ring[ts_head].id = id;
    ts_ring[ts_head].flags = flags;
    ts_ring[ts_head].tv_sec = tv.tv_sec;
    ts_ring[ts_head].tv_usec = tv.tv_usec;

    /* update ring buffer positions */
    ts_head = (ts_head+1)%ts_ring_size;
    if(ts_head == ts_tail)
    {
	ts_tail = (ts_tail+1)%ts_ring_size;
    }

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

