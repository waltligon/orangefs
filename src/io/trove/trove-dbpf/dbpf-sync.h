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

typedef struct
{
    DB *dbp;
    DBT key;
    DBT data;
    struct qlist_head link;
}dbpf_txn_entry_t;

typedef struct
{
    int sync_counter;
    int non_sync_counter;
    int coalesce_counter;
    gen_mutex_t mutex;
    dbpf_op_queue_p op_queue;
    struct qlist_head *txn_queue;
} dbpf_txn_context_t;

int dbpf_sync_context_init(int context_index);
void dbpf_sync_context_destroy(int context_index);

int dbpf_sync_coalesce(dbpf_queued_op_t *qop_p, int retcode, int * outcount);
int dbpf_sync_coalesce_dequeue(dbpf_queued_op_t *qop_p);
int dbpf_sync_coalesce_enqueue(dbpf_queued_op_t *qop_p);


void dbpf_queued_op_set_sync_high_watermark(int high, struct dbpf_collection* coll);
void dbpf_queued_op_set_sync_low_watermark(int low, struct dbpf_collection* coll);

void dbpf_queued_op_set_sync_mode(int enabled, struct dbpf_collection* coll);

void dbpf_db_replication_start(int is_master, struct dbpf_collection *coll);

int dbpf_txn_context_init(int context_index);
void dbpf_txn_context_destroy(int context_index);

int dbpf_txn_coalesce(dbpf_queued_op_t *qop_p, int retcode, int *outcount);
int dbpf_txn_coalesce_dequeue(dbpf_queued_op_t *qop_p);
int dbpf_txn_coalesce_enqueue(dbpf_queued_op_t *qop_p);

int dbpf_txn_queue_add(TROVE_context_id context_index, DB *dbp, DBT *key, DBT *data);
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */ 
