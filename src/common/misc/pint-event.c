/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdarg.h>
#include "pint-event.h"
#include "pint-textlog.h"
#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "gossip.h"
#include "id-generator.h"

#define PINT_EVENT_DEFAULT_TEXTLOG_FILENAME "/tmp/pvfs2-events-log.txt"

/* variables that provide runtime control over which events are recorded */
int PINT_event_on = 0;
int32_t PINT_event_api_mask = 0;
int32_t PINT_event_op_mask = 0;

/* global data structures for storing measurements */
static struct PVFS_mgmt_event* ts_ring = NULL;
static int ts_head = 0;
static int ts_tail = 0;
static int ts_ring_size = 0;
static gen_mutex_t event_mutex = GEN_MUTEX_INITIALIZER;

#ifdef HAVE_MPE
int PINT_event_job_start, PINT_event_job_stop;
int PINT_event_trove_rd_start, PINT_event_trove_wr_start;
int PINT_event_trove_rd_stop, PINT_event_trove_wr_stop;
int PINT_event_bmi_start, PINT_event_bmi_stop;
int PINT_event_flow_start, PINT_event_flow_stop;
#endif

#if defined(HAVE_MPE)
/*
 * PINT_event_mpe_init
 *   initialize the mpe profiling interface
 */
int PINT_event_mpe_init(void)
{
    MPI_Init(NULL, NULL);
    MPE_Init_log();

    PINT_event_job_start = MPE_Log_get_event_number();
    PINT_event_job_stop = MPE_Log_get_event_number();
    PINT_event_trove_rd_start = MPE_Log_get_event_number();
    PINT_event_trove_wr_start = MPE_Log_get_event_number();
    PINT_event_trove_rd_stop = MPE_Log_get_event_number();
    PINT_event_trove_wr_stop = MPE_Log_get_event_number();
    PINT_event_bmi_start = MPE_Log_get_event_number();
    PINT_event_bmi_stop = MPE_Log_get_event_number();
    PINT_event_flow_start = MPE_Log_get_event_number();
    PINT_event_flow_stop = MPE_Log_get_event_number();


    MPE_Describe_state(PINT_event_job_start, PINT_event_job_stop, "Job", "red");
    MPE_Describe_state(PINT_event_trove_rd_start, PINT_event_trove_rd_stop, 
	    "Trove Read", "orange");
    MPE_Describe_state(PINT_event_trove_wr_start, PINT_event_trove_wr_stop, 
	    "Trove Write", "blue");
    MPE_Describe_state(PINT_event_bmi_start, PINT_event_bmi_stop, 
	    "BMI", "yellow");
    MPE_Describe_state(PINT_event_flow_start, PINT_event_flow_stop, 
	    "Flow", "green");

    return 0;
}

void PINT_event_mpe_finalize(void)
{
    /* TODO: use mkstemp like pablo_finalize does */
    MPE_Finish_log("/tmp/pvfs2-server");
    MPI_Finalize();
    return;
}
#endif

#if defined(HAVE_PABLO)
/* PINT_event_pablo_init
 *   initialize the pablo trace library 
 */
int PINT_event_pablo_init(void)
{
    char tracefile[PATH_MAX];
    snprintf(tracefile, PATH_MAX, "/tmp/pvfs2-server.pablo.XXXXXX");
    mkstemp(tracefile);
    setTraceFileName(tracefile);
    return 0;
}

void PINT_event_pablo_finalize(void)
{
    endTracing();
}

#endif

int PINT_event_default_init(int ring_size)
{
    if(ts_ring != NULL)
    {
	gen_mutex_unlock(&event_mutex);
	return(-PVFS_EALREADY);
    }

    /* give a reasonable ring buffer size to work with! */
    if(ring_size < 4)
    {
	gen_mutex_unlock(&event_mutex);
	return(-PVFS_EINVAL);
    }

    /* allocate a ring buffer for time stamped events */
    ts_ring = (struct PVFS_mgmt_event*)malloc(ring_size
	*sizeof(struct PVFS_mgmt_event));
    if(!ts_ring)
    {
	gen_mutex_unlock(&event_mutex);
	return(-PVFS_ENOMEM);
    }
    memset(ts_ring, 0, ring_size*sizeof(struct PVFS_mgmt_event));

    ts_head = 0;
    ts_tail = 0;
    ts_ring_size = ring_size;

    return 0;
}

void PINT_event_default_finalize(void)
{
    if(ts_ring == NULL)
    {
	gen_mutex_unlock(&event_mutex);
	return;
    }

    free(ts_ring);
    ts_ring = NULL;
    ts_head = 0;
    ts_tail = 0;
    ts_ring_size = 0;
}


/* PINT_event_initialize()
 *
 * starts up the event logging interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_event_initialize(int ring_size)
{
    gen_mutex_lock(&event_mutex);

#if defined(HAVE_PABLO)
    PINT_event_pablo_init();
#endif
#if defined(HAVE_MPE)
    PINT_event_mpe_init();
#endif
   
    PINT_event_default_init(ring_size);

    gen_mutex_unlock(&event_mutex);
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
    
    gen_mutex_lock(&event_mutex);
#if defined(HAVE_PABLO)
    PINT_event_pablo_finalize();
#endif
#if defined(HAVE_MPE)
    PINT_event_mpe_finalize();
#endif

    PINT_event_default_finalize();

    gen_mutex_unlock(&event_mutex);
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
    gen_mutex_lock(&event_mutex);

    PINT_event_on = event_on;
    PINT_event_api_mask = api_mask;
    PINT_event_op_mask = op_mask;

    gen_mutex_unlock(&event_mutex);
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
    gen_mutex_lock(&event_mutex);

    *event_on = PINT_event_on;
    *api_mask = PINT_event_api_mask;
    *op_mask = PINT_event_op_mask;

    gen_mutex_unlock(&event_mutex);
    return;
}

void __PINT_event_default(enum PVFS_event_api api,
			  int32_t operation,
			  int64_t value,
			  PVFS_id_gen_t id,
			  int8_t flags,
                          const char * state_name,
                          PVFS_id_gen_t req_id)
{
    PVFS_id_gen_t state_id;
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
    id_gen_fast_register(&state_id, (void *)state_name);
    ts_ring[ts_head].state_id = state_id;
    ts_ring[ts_head].req_id = req_id;
    
    /* update ring buffer positions */
    ts_head = (ts_head+1)%ts_ring_size;
    if(ts_head == ts_tail)
    {
	ts_tail = (ts_tail+1)%ts_ring_size;
    }
}

#ifdef HAVE_PABLO
/* enter a pablo trace into the log */
void __PINT_event_pablo(enum PVFS_event_api api,
			int32_t operation,
			int64_t value,
			PVFS_id_gen_t id,
			int8_t flags,
                        const char * sn,
                        PVFS_id_gen_t req_id)
{
    /* TODO: this can all go once there is a nice "enum to string" function */
    char description[100];
    switch(api) {
	case PVFS_EVENT_API_BMI:
	    sprintf(description, "bmi operation");
	    break;
	case PVFS_EVENT_API_JOB:
	    sprintf(description, "job operation");
	    break;
	case PVFS_EVENT_API_TROVE:
	    sprintf(description, "trove operation");
	    break;
	default:
	    /* TODO: someone fed us a bad api */
    }

    /* PVFS_EVENT_API_BMI, operation(SEND|RECV), value, id, FLAG (start|end) */
    /* our usage better maps to the "startTimeEvent/endTimeEvent" model */
    switch(flags) {
	case PVFS_EVENT_FLAG_START:
	    startTimeEvent( ((api<<6)&(operation<<3)) );
	    traceEvent( ( (api<<6) & (operation<<3) & flags), 
		    description, strlen(description));
	    break;
	case PVFS_EVENT_FLAG_END:
	    endTimeEvent( ((api<6)&(operation<3)) );
	    break;
	default:
	    /* TODO: someone fed us bad flags */
    }


}
#endif

#if defined(HAVE_MPE)
void __PINT_event_mpe(enum PVFS_event_api api,
		      int32_t operation,
		      int64_t value,
		      PVFS_id_gen_t id,
		      int8_t flags,
                      const char * sn,
                      PVFS_id_gen_t req_id)
{
    switch(api) {
	case PVFS_EVENT_API_BMI:
	    if (flags & PVFS_EVENT_FLAG_START) {
		MPE_Log_event(PINT_event_bmi_start, 0, NULL);
	    } else if (flags & PVFS_EVENT_FLAG_END) {
		MPE_Log_event(PINT_event_bmi_stop, value, NULL);
	    }
	case PVFS_EVENT_API_JOB:
	    if (flags & PVFS_EVENT_FLAG_START) {
		MPE_Log_event(PINT_event_job_start, 0, NULL);
	    } else if (flags & PVFS_EVENT_FLAG_END) {
		MPE_Log_event(PINT_event_job_stop, value, NULL);
	    }
	case PVFS_EVENT_API_TROVE:
	    if (flags & PVFS_EVENT_FLAG_START) {
		MPE_Log_event(PINT_event_trove_start, 0, NULL);
	    } else if (flags & PVFS_EVENT_FLAG_END) {
		MPE_Log_event(PINT_event_trove_stop, value, NULL);
	    }
    }

}
#endif

/* PINT_event_timestamp()
 *
 * records a timestamp in the ring buffer
 *
 * returns 0 on success, -PVFS_error on failure
 */
void __PINT_event_timestamp(enum PVFS_event_api api,
			    int32_t operation,
			    int64_t value,
			    PVFS_id_gen_t id,
			    int8_t flags,
                            const char * state_name,
                            PVFS_id_gen_t req_id)
{
    gen_mutex_lock(&event_mutex);

#if defined(HAVE_PABLO)
    __PINT_event_pablo(api, operation, value, id, flags, state_name, req_id);
#endif

#if defined (HAVE_MPE)
    __PINT_event_mpe(api, operation, value, id, flags, state_name, req_id);
#endif

    __PINT_event_default(api, operation, value, id, flags, state_name, req_id);

    gen_mutex_unlock(&event_mutex);

    return;
}

/* PINT_event_retrieve()
 *
 * fills in an array with current snapshot of event buffer
 *
 * no return value
 */
void PINT_event_retrieve(struct PVFS_mgmt_event* event_array,
			 int count)
{
    int tmp_tail = ts_tail;
    int cur_index = 0;
    int i;

    gen_mutex_lock(&event_mutex);

    /* copy out any events from the ring buffer */
    while(tmp_tail != ts_head && cur_index < count)
    {
	event_array[cur_index] = ts_ring[tmp_tail];
	tmp_tail = (tmp_tail+1)%ts_ring_size;
	cur_index++;
    }

    gen_mutex_unlock(&event_mutex);

    /* fill in remainder of array with invalid flag */
    for(i=cur_index; i<count; i++)
    {
	event_array[i].flags = PVFS_EVENT_FLAG_INVALID;
    }

    return;
}

void PINT_event_log_events(
    struct PVFS_mgmt_event * events, 
    int count,
    const char * filename)
{
    struct PVFS_mgmt_event * event_array = events;
    int array_size = count;
    const char * out_filename = filename;
   
    if(!PINT_event_on)
    {
        return;
    }
    
    if(!events)
    {
        /* assume we want to log server's events locally */
        event_array = (struct PVFS_mgmt_event*)malloc(
            ts_ring_size*sizeof(struct PVFS_mgmt_event));
        PINT_event_retrieve(event_array, ts_ring_size);
        array_size = ts_ring_size;
        out_filename = PINT_EVENT_DEFAULT_TEXTLOG_FILENAME;
    }

    gen_mutex_lock(&event_mutex);
    PINT_textlog_generate(event_array, array_size, out_filename);
    gen_mutex_unlock(&event_mutex);
}

const char * PVFS_event_api_names[PVFS_EVENT_API_COUNT] =
{
    "JOB",
    "BMI",
    "TROVE",
    "ENCODE_REQ",
    "ENCODE_RESP",
    "DECODE_REQ",
    "DECODE_RESP",
    "SM",
    "STATES"
};

const char * PVFS_event_op_names[PVFS_EVENT_OP_COUNT] =
{
     "BMI_SEND",
     "BMI_RECV",
     "FLOW",
     "TROVE_READ_AT",
     "TROVE_WRITE_AT",
     "TROVE_BSTREAM_FLUSH",
     "TROVE_KEYVAL_FLUSH",
     "TROVE_READ_LIST",
     "TROVE_WRITE_LIST",
     "TROVE_KEYVAL_READ",
     "TROVE_KEYVAL_READ_LIST",
     "TROVE_KEYVAL_WRITE",
     "TROVE_DSPACE_GETATTR",
     "TROVE_DSPACE_SETATTR",
     "TROVE_BSTREAM_RESIZE",
     "TROVE_KEYVAL_REMOVE",
     "TROVE_KEYVAL_ITERATE",
     "TROVE_KEYVAL_ITERATE_KEYS",
     "TROVE_DSPACE_ITERATE_HANDLES",
     "TROVE_DSPACE_CREATE",
     "TROVE_DSPACE_REMOVE",
     "TROVE_DSPACE_VERIFY",
     "TROVE_BSTREAM_VALIDATE",
     "TROVE_KEYVAL_VALIDATE"
};  


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
