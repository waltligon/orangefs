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
    int coalesce_counter;
    gen_mutex_t * mutex;
    dbpf_op_queue_p sync_queue;
} dbpf_sync_context_t;

int dbpf_sync_context_init(int context_index);
void dbpf_sync_context_destroy(int context_index);
int dbpf_sync_coalesce(dbpf_queued_op_t *qop_p);
int dbpf_sync_coalesce_dequeue(dbpf_queued_op_t *qop_p);
int dbpf_sync_coalesce_enqueue(dbpf_queued_op_t *qop_p);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */ 
