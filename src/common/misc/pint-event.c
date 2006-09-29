/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>

#include <stdio.h>
#include <assert.h>

#include "pint-event.h"
#include "pvfs2-types.h"
#include "pvfs2-mgmt.h"
#include "gossip.h"
#include "server-config.h"

/* logs a lot more than default :) */
#define MPE_EXTENDED_LOGGING

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
int PINT_event_trove_rd_start, PINT_event_trove_rd_stop;
int PINT_event_trove_wr_start, PINT_event_trove_wr_stop;
int PINT_event_bmi_start, PINT_event_bmi_stop;
int PINT_event_flow_start, PINT_event_flow_stop;
int PINT_event_unexpected_decode;

int * PINT_event_perf_counter_update = 0; /* event number for perf counters*/
char ** PINT_event_perf_counter_key_names = 0;
int PINT_event_perf_counter_keys = 0;

extern char * mpe_logfile;
#endif

void PINT_event_initalize_perf_counter_events(int number){
    PINT_event_perf_counter_update = calloc(number,sizeof(int));
    PINT_event_perf_counter_key_names = calloc(number,sizeof(char *));
}

/*
 * register the performance counter values:
 */
void PINT_event_register_perf_counter_event(const char * keyName){
    /* add new key name: */ 
    PINT_event_perf_counter_key_names[PINT_event_perf_counter_keys] = 
        malloc(strlen(keyName)+1);
    strcpy(PINT_event_perf_counter_key_names[PINT_event_perf_counter_keys], keyName);
    
    PINT_event_perf_counter_keys++;     
}

/* PINT_event_initialize()
 *
 * starts up the event logging interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_event_initialize(int ring_size, int enable_on_startup)
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
    
    if ( enable_on_startup )
    {
       PINT_event_set_masks(1, ~0, ~0);
    }
    return(0);
}

#if defined(HAVE_MPE)
void PINT_event_init_mpe_log_file(void);
void PINT_event_finish_log(const char * logfile);

void PINT_event_init_mpe_log_file(void)
{
    char * formatString;
    int i;
	MPE_Init_log();

    MPE_Log_get_solo_eventID(&PINT_event_job_start);
    MPE_Log_get_solo_eventID(&PINT_event_job_stop);

    MPE_Log_get_solo_eventID(&PINT_event_trove_rd_start);
    MPE_Log_get_solo_eventID(&PINT_event_trove_rd_stop);
 
    MPE_Log_get_solo_eventID(&PINT_event_trove_wr_start);
    MPE_Log_get_solo_eventID(&PINT_event_trove_wr_stop);
     
    MPE_Log_get_solo_eventID(&PINT_event_bmi_start);
    MPE_Log_get_solo_eventID(&PINT_event_bmi_stop);
     
    MPE_Log_get_solo_eventID(&PINT_event_flow_start);
    MPE_Log_get_solo_eventID(&PINT_event_flow_stop);
     
    MPE_Log_get_solo_eventID(&PINT_event_unexpected_decode);
    
#ifdef MPE_EXTENDED_LOGGING
    for (i = 0 ; i < PINT_event_perf_counter_keys ; i++){
        char event_name[255];
        MPE_Log_get_solo_eventID(& PINT_event_perf_counter_update[i]);
        sprintf(event_name, "PC:%s", PINT_event_perf_counter_key_names[i]);
        MPE_Describe_info_event(PINT_event_perf_counter_update[i], event_name, 
            "white", "value=%l");
    }

    formatString ="comm=%d,rank=%d,cid=%d,op=%d,value=%l"; /*,job-id=%l*/
#else 
    formatString ="";
#endif
    
    MPE_Describe_info_event(PINT_event_job_start, "Job (start)", "green1", formatString);
    MPE_Describe_info_event(PINT_event_job_stop, "Job (end)", "green3", formatString);
    
    MPE_Describe_info_event(PINT_event_trove_rd_start, "Trove read (start)", "blue1", formatString);
    MPE_Describe_info_event(PINT_event_trove_rd_stop, "Trove read (end)", "blue3", formatString);
    MPE_Describe_info_event(PINT_event_trove_wr_start, "Trove write (start)", "orange1", formatString);
    MPE_Describe_info_event(PINT_event_trove_wr_stop, "Trove write (end)", "orange3", formatString);
     
    
    MPE_Describe_info_event(PINT_event_bmi_start, "BMI (start)", "yellow3", formatString);
    MPE_Describe_info_event(PINT_event_bmi_stop, "BMI (end)", "yellow1", formatString);
    MPE_Describe_info_event(PINT_event_flow_start, "Flow (start)", "red3", formatString);
    MPE_Describe_info_event(PINT_event_flow_stop, "Flow (end)", "red1", formatString);
    
    MPE_Describe_info_event(PINT_event_unexpected_decode, "Request decode", "gray", formatString);
}

void PINT_event_finish_log(const char * logfile)
{
    MPE_Finish_log(logfile);
}

/*
 * PINT_event_mpe_init
 *   initialize the mpe profiling interface
 */
int PINT_event_mpe_init(void)
{
    if (mpe_logfile == NULL)
    {
        const char buf[] = "/tmp/pvfs2-mpe-log";
        mpe_logfile = strdup(buf);
    }
    
    PMPI_Init(NULL, NULL);    
    PINT_event_init_mpe_log_file();
    
    return 0;
}

void PINT_event_mpe_finalize(void)
{
    if (PINT_event_on)
    {
        gossip_err("Writing clog2 to: %s \n", mpe_logfile);
        PINT_event_finish_log(mpe_logfile);
    }
    PMPI_Finalize();
    return;
}
#endif /* HAVE_MPE */

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
#if defined(HAVE_MPE)
     if(PINT_event_on && ! event_on){ 
        gossip_err("Writing clog2 to: %s \n", mpe_logfile);
        PINT_event_finish_log(mpe_logfile);
        PINT_event_init_mpe_log_file();
     }
#endif
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
                PVFS_hint * hints)
{
    gen_mutex_lock(&event_mutex);

#if defined(HAVE_PABLO)
    __PINT_event_pablo(api, operation, value, id, flags);
#endif

#if defined (HAVE_MPE)
    __PINT_event_mpe(api, operation, value, id, flags, hints);
#endif

    __PINT_event_default(api, operation, value, id, flags);

    gen_mutex_unlock(&event_mutex);

    return;
}

void __PINT_event_default(enum PVFS_event_api api,
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
}

#ifdef HAVE_PABLO
/* enter a pablo trace into the log */
void __PINT_event_pablo(enum PVFS_event_api api,
			int32_t operation,
			int64_t value,
			PVFS_id_gen_t id,
			int8_t flags)
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
        case PVFS_EVENT_API_ENCODE_REQ:
        case PVFS_EVENT_API_ENCODE_RESP:
        case PVFS_EVENT_API_DECODE_REQ:
        case PVFS_EVENT_API_DECODE_RESP:
        case PVFS_EVENT_API_SM:
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
              PVFS_hint * hints)
{
/*
 * TODO figure out if the size of the MPE format string
 *  depends on the architecture.
 */    
    MPE_LOG_BYTES  bytebuf;      /* buffer for logging of flags */
    int    bytebuf_pos = 0;
    const char * request_id;
    char hostname[20] = "";       /* not in log for buffer size limit */
    int comm = -1, rank = -1, call_id = -1; /* 3 x 4 = 12 bytes in buffer */
    char io_type[20] = "";        /* longest io_type has 19 characters */
            
#ifdef MPE_EXTENDED_LOGGING
    if(api == PVFS_EVENT_API_PERFORMANCE_COUNTER){
        MPE_Log_pack( bytebuf, &bytebuf_pos, 'l', 1, &value );

        assert(operation <= PINT_event_perf_counter_keys);
        MPE_Log_event(PINT_event_perf_counter_update[operation], 0, bytebuf);
        return;
    }    

    request_id = PVFS_get_hint( hints, REQUEST_ID);
    if(request_id != NULL){
        /* In mpe.h size max 4 * sizeof(double) -> MPE_SIZE_BYTES */         
        /* gossip_err("EVENT request ID received: %s\n", request_id); */       
        char *saveptr1, *saveptr2; /* for thread safe strtok_r() */
    
        char *alloc_parseid = strdup(request_id);
        char *parseid = alloc_parseid; /* for free() */
    
        char *rid_name, *rid_value;
    
        while(1)
        {
            rid_name = strtok_r(parseid, ",", &saveptr1);
    
            parseid = NULL;
            if (rid_name == NULL)
            {
                break;
            }
    
            strtok_r(rid_name, ":", &saveptr2);
            rid_value = strtok_r(NULL, ":", &saveptr2);
    
            if(!strcmp(rid_name, "host"))
               sscanf(rid_value, "%20s", hostname);
            else if(!strcmp(rid_name, "comm"))
                sscanf(rid_value, "%d", &comm);
            else if(!strcmp(rid_name, "rank"))
                sscanf(rid_value, "%d", &rank);
            else if(!strcmp(rid_name, "id"))
                sscanf(rid_value, "%d", &call_id);
            else if(!strcmp(rid_name, "op"))
                sscanf(rid_value, "%20s", io_type);
        }
        free(alloc_parseid);
        if ( strlen(io_type) > 18 )  /* max 18 + 2 = 20 bytes in buffer */
           io_type[18] = '\0';
        
        if( hostname[0] == 0 )
        {
            gossip_err("pint-event.c unparsable request id in hints:%s\n", request_id);
        }
/* DEBUG ME IF NECCESSARY        
        else
        {
            gossip_err("pint-event.c: host:%s,comm:%d,rank:%d,id:%d,op:%s\n",
                hostname, comm, rank, call_id, io_type);
        }
*/        
    }


    /* now log stuff to bytebuffer */
    MPE_Log_pack( bytebuf, &bytebuf_pos, 'd', 1, &comm );
    MPE_Log_pack( bytebuf, &bytebuf_pos, 'd', 1, &rank );    
    MPE_Log_pack( bytebuf, &bytebuf_pos, 'd', 1, &call_id );
    /* MPE_Log_pack( bytebuf, &bytebuf_pos, 's', strlen(io_type), io_type ); */    

    MPE_Log_pack( bytebuf, &bytebuf_pos, 'd', 1, &operation ); 
    MPE_Log_pack( bytebuf, &bytebuf_pos, 'l', 1, &value );
    /*does not fit... MPE_Log_pack( bytebuf, &bytebuf_pos, 'l', 1, &id ); */
#endif

    switch(api) {
    case PVFS_EVENT_API_FLOW:
        if (flags & PVFS_EVENT_FLAG_START) {
            MPE_Log_event(PINT_event_flow_start, 0, bytebuf);
        } else if (flags & PVFS_EVENT_FLAG_END) {
            MPE_Log_event(PINT_event_flow_stop, 0, bytebuf);
        }
       break;
    case PVFS_EVENT_API_BMI:
        if (flags & PVFS_EVENT_FLAG_START) {
            MPE_Log_event(PINT_event_bmi_start, 0, bytebuf); //second parameter seems to be unused in MPE2 !!!
        } else if (flags & PVFS_EVENT_FLAG_END) {
            MPE_Log_event(PINT_event_bmi_stop, 0, bytebuf);
        }
        break;
    case PVFS_EVENT_API_JOB:
        bytebuf_pos = 0;        
        if (flags & PVFS_EVENT_FLAG_START) {
            MPE_Log_event(PINT_event_job_start, 0, bytebuf);
        } else if (flags & PVFS_EVENT_FLAG_END) {
            MPE_Log_event(PINT_event_job_stop, 0, bytebuf);
        }
        break;
    case PVFS_EVENT_API_TROVE:
        if (flags & PVFS_EVENT_FLAG_START) {
            if (operation == PVFS_EVENT_TROVE_READ_LIST) {
                MPE_Log_event(PINT_event_trove_rd_start, 0, bytebuf);
            } else if (operation == PVFS_EVENT_TROVE_WRITE_LIST) {
                MPE_Log_event(PINT_event_trove_wr_start, 0, bytebuf);
                }
        } else if (flags & PVFS_EVENT_FLAG_END) {
            if (operation == PVFS_EVENT_TROVE_READ_LIST) {
                MPE_Log_event(PINT_event_trove_rd_stop, 0, bytebuf);
            } else if (operation == PVFS_EVENT_TROVE_WRITE_LIST) {
                MPE_Log_event(PINT_event_trove_wr_stop, 0, bytebuf);
            }
        }
        break;
    case PVFS_EVENT_API_DECODE_UNEXPECTED:
        MPE_Log_event(PINT_event_unexpected_decode, 0, bytebuf);
        break;
    case PVFS_EVENT_API_PERFORMANCE_COUNTER:        
    case PVFS_EVENT_API_DECODE_REQ:
    case PVFS_EVENT_API_ENCODE_REQ:
    case PVFS_EVENT_API_ENCODE_RESP:            
    case PVFS_EVENT_API_DECODE_RESP:
    case PVFS_EVENT_API_SM:
        break; /* XXX: NEEDS SOMETHING */
    }
}
#endif

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

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
