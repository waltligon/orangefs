/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dbpf-op-queue.h"
#include "gossip.h"
#include "pint-perf-counter.h"

typedef struct
{
    int sync_counter;
    /*
     * Number of operations which are modifying, but are not marked with
     * TROVE_Sync
     */
	int non_sync_counter;
	    
    int coalesce_counter;
    
    gen_mutex_t mutex;
    dbpf_op_queue_p sync_queue;
} dbpf_sync_context_t;

int dbpf_sync_context_init(int context_index);
void dbpf_sync_context_destroy(int context_index);

int dbpf_sync_coalesce(dbpf_queued_op_t *qop_p, int retcode, int * outcount);
int dbpf_sync_coalesce_dequeue(dbpf_queued_op_t *qop_p);
int dbpf_sync_coalesce_enqueue(dbpf_queued_op_t *qop_p);


void dbpf_queued_op_set_sync_high_watermark(int high, struct dbpf_collection* coll);
void dbpf_queued_op_set_sync_low_watermark(int low, struct dbpf_collection* coll);

void dbpf_queued_op_set_sync_mode(int enabled, struct dbpf_collection* coll);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */ 
