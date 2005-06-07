/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This code generates a textlog in the text log format
 * defined by the slog2sdk package.  This allows us to write
 * events out easily and later convert them to slog2 using
 * textlogTOslog2.  Although slog2 is primarily used to show
 * event traces of different *nodes* in an MPI program, we're
 * using it to show event traces of states, functions, and async
 * I/0 calls for different *requests* within the same server
 * process.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>

#include "id-generator.h"
#include "pvfs2-event.h"
#include "pvfs2-mgmt.h"
#include "gossip.h"
#include "quickhash.h"
#include "pint-event.h"

#define STATE_TABLE_SIZE 503

static char * PINT_event_server_operation_names[29] =
{
	"INVALID",
	"CREATE",
	"REMOVE",
	"IO",
	"GETATTR",
	"SETATTR",
	"LOOKUP_PATH",
	"CRDIRENT",
	"RMDIRENT",
	"CHDIRENT",
	"TRUNCATE",
	"MKDIR",
	"READDIR",
	"GETCONFIG",
	"WRITE_COMPLETION",
	"FLUSH",
	"MGMT_SETPARAM",
	"MGMT_NOOP",
	"STATFS",
	"PERF_UPDATE",
	"MGMT_PERF_MON",
	"MGMT_ITERATE_HANDLES",
	"MGMT_DSPACE_INFO_LIST",
	"MGMT_EVENT_MON",
	"MGMT_REMOVE_OBJECT",
	"MGMT_REMOVE_DIRENT",
	"MGMT_GET_DIRDATA_HANDLE",
	"JOB_TIMER",
	"PROTO_ERROR",
};

static int PINT_event_log2(int _val)
{
    int res = 0;
    while(!(_val & (1 << res)))
    {
        ++res;
    }
    return res;
}

#define PINT_event_api_get_name(_api) PVFS_event_api_names[PINT_event_log2(_api)]

#define PINT_event_op_get_name(_op) PVFS_event_op_names[_op - 1]

/* We need to keep track of the start events
 * so that we can match them up with the end events
 * when they occur
 */
typedef struct
{
    struct PVFS_mgmt_event * event;

    struct qlist_head link;
} PINT_textlog_start_event_entry;

/* Each state gets a unique index number
 * in the log, so we keep a state hashtable
 * that maps references of states to their
 * unique index numbers for the log.
 */
typedef struct
{
    uint32_t id;
    int index; /* this is the category number in the textlog */

    struct qhash_head link;
} PINT_textlog_entry;

static inline int PINT_textlog_id_compare(
    void * key,
    struct qhash_head * link)
{
    uint32_t k = (uint32_t)key;
    PINT_textlog_entry * link_entry;
    link_entry = (qhash_entry(link, 
                              PINT_textlog_entry, link));
    
    if(k == link_entry->id)
    {
        return 1;
    }
  
    return 0;
}

static inline int PINT_textlog_id_hash(
    void * key,
    int table_size)
{
    uint32_t k = (uint32_t) key;

    return (k << ((sizeof(uint32_t) * 4)) ^
            (k >> (sizeof(uint32_t) * 4))) % table_size;
}

#define PINT_textlog_make_state_key(_event) \
    ((0xFF000000 & (((uint32_t)_event->operation) << 24)) | \
     (0x00FFFFFF & ((uint32_t)_event->state_id)))

#define PINT_textlog_make_apiop_key(_event) \
    ((0x00FFFFFF & ((uint32_t)_event->api)) | \
     (0xFF000000 & (((uint32_t)_event->operation) << 24)))

#define PINT_textlog_make_key(_event) \
    ((_event->api & PVFS_EVENT_API_STATES) ? \
        PINT_textlog_make_state_key(_event) : \
        PINT_textlog_make_apiop_key(_event))

static inline int PINT_textlog_id_table_lookup(
    struct qhash_table * id_map,
    struct PVFS_mgmt_event * event)
{
    void * key;
    PINT_textlog_entry * entry;
    struct qhash_head * qres;

    key = (void *)PINT_textlog_make_key(event);

    qres = qhash_search(id_map, key);
    if(qres)
    {
        entry = qhash_entry(qres, PINT_textlog_entry, link);
        return entry->index;
    }

    return -1;
}
 
static inline int PINT_textlog_state_table_add(
    struct qhash_table * state_table,
    struct PVFS_mgmt_event * event,
    int index)
{
    PINT_textlog_entry * entry;

    entry = qhash_malloc(sizeof(PINT_textlog_entry));
    if(!entry)
    {
        gossip_err("PINT_textlog_state_table_add: Out of Memory Error: "
                   "malloc(%d)\n", sizeof(PINT_textlog_entry));
        return -1;
    }
    memset(entry, 0, sizeof(PINT_textlog_entry));

    entry->id = PINT_textlog_make_key(event);

    entry->index = index;
    INIT_QHASH_HEAD(&entry->link);
    qhash_add(state_table, (void *)entry->id, &entry->link);


    return 0;
}
  
static inline void PINT_textlog_state_table_finalize(
    struct qhash_table * table)
{
    PINT_textlog_entry * entry;
    struct qhash_head * tmp_link = NULL;
    
    qhash_for_each(tmp_link, &(table->array[0]))
    {
        entry = qhash_entry(tmp_link, PINT_textlog_entry, link);
        qhash_free(entry);
    }
    qhash_finalize(table);
}

/* writes the state as a category in the textlog format
 */
static inline int PINT_textlog_write_statename(
    FILE * fp,
    struct PVFS_mgmt_event * event,
    struct qhash_table * state_table)
{
    int res;
    int index = PINT_textlog_id_table_lookup(
        state_table, event);
    
    res = fprintf(fp,
            "Category[ index=%d name=", index);
    if(res < 0)
    {
        return res; 
    }
    
    if(event->api & PVFS_EVENT_API_STATES)
    {
        int res;
    
        res = fprintf(fp, "%s:%s", 
                      (event->operation < 26) ?
                      PINT_event_server_operation_names[event->operation] :
                      "NULL",
                      (char *)id_gen_fast_lookup(event->state_id));
        if(res < 0)
        {
            return res;
        }
        
        res = fprintf(fp,
                      " topo=State "
                      "color=(%d,%d,%d,127,true) width=1 ]\n",
                      rand() % 255,
                      rand() % 255,
                      rand() % 255);
        if(res < 0)
        {
            return res;
        }
    }
    else
    {
        res = fprintf(fp,
                      "%s:%s topo=State "
                      "color=(%d,%d,%d,127,true) width=1 ]\n",
                      PINT_event_api_get_name(event->api),
                      PINT_event_op_get_name(event->operation),
                      rand() % 255,
                      rand() % 255,
                      rand() % 255);
        if(res < 0)
        {
            return res;
        }
    }

    return 0;
}

#define PINT_textlog_timeval_to_secs(_epoc, _event) \
    (((float)(_event->tv_sec - _epoc->tv_sec)) + \
     (((float)(_event->tv_usec - _epoc->tv_usec))*1e-6))

static int PINT_textlog_write_event(
    FILE * fp,
    struct timeval * epoc,
    struct PVFS_mgmt_event * estart,
    struct PVFS_mgmt_event * eend,
    struct qhash_table * state_table)
{
    float starttime, endtime;
    int req_index;
    
    req_index = (int)id_gen_fast_lookup(estart->req_id);
    
    starttime = PINT_textlog_timeval_to_secs(epoc, estart);
    endtime = PINT_textlog_timeval_to_secs(epoc, eend);
   
    return fprintf(
        fp,
        "Primitive[ TimeBBox(%.10f,%.10f) "
        "Category=%d (%.10f, %d) (%.10f, %d) ]\n",
        starttime, endtime,
        PINT_textlog_id_table_lookup(state_table, estart),
        starttime,
        req_index,
        endtime,
        req_index);
}

static inline int PINT_textlog_start_events_insert(
    struct qlist_head * events_list,
    struct PVFS_mgmt_event * event)
{
    PINT_textlog_start_event_entry * entry;

    entry = malloc(sizeof(PINT_textlog_start_event_entry));
    if(!entry)
    {
        gossip_err("PINT_textlog_start_events_insert: Out of Memory\n");
        return -1;
    }
    memset(entry, 0, sizeof(PINT_textlog_start_event_entry));

    entry->event = event;
    INIT_QLIST_HEAD(&entry->link);
    qlist_add(&entry->link, events_list);

    return 0;
}

static inline 
struct PVFS_mgmt_event * PINT_textlog_find_matching_start_event(
    struct qlist_head * events_list,
    struct PVFS_mgmt_event * end_event)
{
    struct PVFS_mgmt_event * event_entry;
    PINT_textlog_start_event_entry * entry;
    struct qlist_head * iter;
    uint32_t end_key;
    uint32_t iter_key;

    end_key = PINT_textlog_make_key(end_event);
    
    qlist_for_each(iter, events_list)
    {
        entry = (qlist_entry(
                iter, PINT_textlog_start_event_entry, link));
        iter_key = PINT_textlog_make_key(entry->event);
        
        if(entry->event->req_id == end_event->req_id &&
           end_key == iter_key)
        {
            event_entry = entry->event;
            qlist_del(&entry->link);
            free(entry);
            return event_entry;
        }
    }

    return NULL;
}

static inline void PINT_textlog_start_events_list_finalize(
    struct qlist_head * events)
{
    PINT_textlog_start_event_entry * se_entry;
    struct qlist_head * entry;

    for(entry = events->next; entry != events;)
    {
        se_entry = qlist_entry(
            entry, PINT_textlog_start_event_entry, link);
        entry = entry->next;
        free(se_entry);
    }
}

void PINT_textlog_generate(
    struct PVFS_mgmt_event * events,
    int count,
    const char * filename)
{
    FILE * outfp;
    struct timeval epoc;
    int tr_index; 
    struct qhash_table * id_map;
    QLIST_HEAD(start_events_list);
    int textlog_state_index = 0;
    
    outfp = fopen(filename, "w");
    if(!outfp)
    {
        gossip_err("PINT_generate_textlog: Failed to open file %s: %s\n",
                   filename, strerror(errno));
        return;
    }

    id_map = qhash_init(PINT_textlog_id_compare, 
			PINT_textlog_id_hash, 
			STATE_TABLE_SIZE);
    if(!id_map)
    {
	gossip_err("PINT_generate_textlog: qhash_init failed for state map "
		   "with table size of %d\n", STATE_TABLE_SIZE);
	goto close_fp;
    }

    for(tr_index = 0; tr_index < count; ++tr_index)
    {
        if(events[tr_index].flags != PVFS_EVENT_FLAG_INVALID)
	{
            /* find all the possible unique state values 
             * and create indexes, colors and
             * write out the categories for each
             */
           
            /* lookup this state name to see if its already defined */
            if(PINT_textlog_id_table_lookup(
                    id_map, &events[tr_index]) == -1)
            {
                if(PINT_textlog_state_table_add(
                    id_map,
                    &events[tr_index],
                    ++textlog_state_index) < 0)
                {
                    goto finalize_id_map;
                }

                if(PINT_textlog_write_statename(
                    outfp, 
                    &events[tr_index], 
                    id_map) < 0)
                {
                    goto finalize_id_map;
                }
            }
        }
    }

    /* get first timestamp - we assume all timestamps occur
     * after the first event in the array.
     * */
    if(events[0].flags != PVFS_EVENT_FLAG_INVALID)
    {
        epoc.tv_sec = events[0].tv_sec;
        epoc.tv_usec = events[0].tv_usec;
    }
    else
    {
        gossip_err("PINT_textlog_generate: invalid event in array[0]\n");
        goto finalize_start_events_list;
    }

    /* now that the categories (state definitions) are written 
     * we can write the actual events with the appropriate request id
     * mapped to node number
     */
    for(tr_index = 0; tr_index < count; tr_index++)
    {
        if(events[tr_index].flags == PVFS_EVENT_FLAG_START)
        {
            /* added started event to the event list */
            if(PINT_textlog_start_events_insert(
                &start_events_list,
                &events[tr_index]) < 0)
            {
                goto finalize_start_events_list;
            }
        }
        else if(events[tr_index].flags == PVFS_EVENT_FLAG_END)
        {
            struct PVFS_mgmt_event * start_event = 
                PINT_textlog_find_matching_start_event(
                    &start_events_list,
                    &events[tr_index]);
            if(!start_event)
            {
                /* hmmm - no matching start event. Just skip
                 * and go to the next one
                 */
                continue;
            }
            
            if(PINT_textlog_write_event(
                outfp, 
                &epoc,
                start_event,
                &events[tr_index], 
                id_map) < 0)
            {
                goto finalize_start_events_list;
            }
        }
    }
   
finalize_start_events_list:
    PINT_textlog_start_events_list_finalize(&start_events_list);
finalize_id_map:
    PINT_textlog_state_table_finalize(id_map);
close_fp:
    fclose(outfp);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
