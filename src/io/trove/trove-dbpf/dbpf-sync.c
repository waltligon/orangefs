/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dbpf-op-queue.h"
#include "gossip.h"
#include "pint-perf-counter.h"
#include "dbpf-sync.h"

static int s_high_watermark = 8;
static int s_low_watermark = 2;

static dbpf_sync_context_t * keyval_sync_array[TROVE_MAX_CONTEXTS];
static dbpf_sync_context_t * dspace_sync_array[TROVE_MAX_CONTEXTS];

extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t *dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];
extern pthread_cond_t dbpf_op_completed_cond;

int dbpf_sync_context_init(int context_index)
{
    keyval_sync_array[context_index] = malloc(sizeof(dbpf_sync_context_t));
    if(!keyval_sync_array[context_index])
    {
        return -TROVE_ENOMEM;
    }

    memset(keyval_sync_array[context_index], 0, sizeof(dbpf_sync_context_t));

    keyval_sync_array[context_index]->mutex = gen_mutex_build();
    keyval_sync_array[context_index]->sync_queue = dbpf_op_queue_new();

    dspace_sync_array[context_index] = malloc(sizeof(dbpf_sync_context_t));
    if(!dspace_sync_array[context_index])
    {
        return -TROVE_ENOMEM;
    }

    memset(dspace_sync_array[context_index], 0, sizeof(dbpf_sync_context_t));

    dspace_sync_array[context_index]->mutex = gen_mutex_build();
    dspace_sync_array[context_index]->sync_queue = dbpf_op_queue_new();
    
    return 0;
}

void dbpf_sync_context_destroy(int context_index)
{
    gen_mutex_destroy(keyval_sync_array[context_index]->mutex);
    dbpf_op_queue_cleanup(keyval_sync_array[context_index]->sync_queue);

    gen_mutex_destroy(dspace_sync_array[context_index]->mutex);
    dbpf_op_queue_cleanup(dspace_sync_array[context_index]->sync_queue);

    free(keyval_sync_array[context_index]);
}
    
int dbpf_sync_coalesce(
    dbpf_queued_op_t *qop_p)
{
    int ret = 0;
    dbpf_sync_context_t * sync_context;
    int sync_counter;
    int * coalesce_counter;
    DB * dbp;

    dbpf_queued_op_t *ready_op;
   
    if(DBPF_OP_IS_KEYVAL(qop_p->op.type))
    {
        dbp = qop_p->op.coll_p->keyval_db;
        sync_context = keyval_sync_array[qop_p->op.context_id];
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: coalescing keyval ops\n");
    }
    else if(DBPF_OP_IS_DSPACE(qop_p->op.type))
    {
        dbp = qop_p->op.coll_p->ds_db;
        sync_context = dspace_sync_array[qop_p->op.context_id];
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: coalescing dspace ops\n");
    }
    else
    {
        return -TROVE_ENOSYS;
    }
    
    if(!qop_p->stats.coalesce_sync)
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]:\toperation synced "
                     "individually (not coalesced)\n"
                     "\t\tcoalesced: %d\n\t\tqueued: %d\n",
                     *coalesce_counter,
                     sync_counter);

        /* do sync right now */
        DBPF_DB_SYNC_IF_NECESSARY((&qop_p->op), dbp, ret);
        if(ret < 0)
        {
            return ret;
        }

        ret = dbpf_queued_op_complete(qop_p, 0, OP_COMPLETED);
        if(ret < 0)
        {
            return ret;
        }

        return 1;
    }
        
   gen_mutex_lock(sync_context->mutex);

    if(sync_context->coalesce_counter > s_high_watermark ||
       sync_context->sync_counter < s_low_watermark)
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]:\thigh or low watermark reached:\n"
                     "\t\tcoalesced: %d\n\t\tqueued: %d\n",
                     sync_context->coalesce_counter,
                     sync_context->sync_counter);

        /* sync this operation and signal completion of the others
         * waiting to be synced
         */
        DBPF_DB_SYNC_IF_NECESSARY((&qop_p->op),  dbp, ret);
        if(ret < 0)
        {
            return ret;
        }

        gen_mutex_lock(
            dbpf_completion_queue_array_mutex[qop_p->op.context_id]);
        dbpf_op_queue_add(
            dbpf_completion_queue_array[qop_p->op.context_id], qop_p);
        gen_mutex_lock(&qop_p->mutex);
        qop_p->op.state = OP_COMPLETED;
        gen_mutex_unlock(&qop_p->mutex);

        qop_p->state = 0;

        /* move remaining ops in queue with ready-to-be-synced state
         * to completion queue
         */
        while(!dbpf_op_queue_empty(sync_context->sync_queue))
        {
            ready_op = dbpf_op_queue_shownext(sync_context->sync_queue);
            dbpf_op_queue_remove(ready_op);
            dbpf_op_queue_add(
                dbpf_completion_queue_array[qop_p->op.context_id], ready_op);
            gen_mutex_lock(&ready_op->mutex);
            ready_op->op.state = OP_COMPLETED;
            gen_mutex_unlock(&ready_op->mutex);

            qop_p->state = 0;

            sync_context->coalesce_counter--;
        }

        pthread_cond_signal(&dbpf_op_completed_cond);
        gen_mutex_unlock(
            dbpf_completion_queue_array_mutex[qop_p->op.context_id]);

        ret = 1;
    }
    else
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]:\tputting on coalescing queue: (%p)\n"
                     "\t\tcoalesced: %d\n\t\tqueued: %d\n",
                     qop_p,
                     sync_context->coalesce_counter,
                     sync_context->sync_counter);

        /* put back on the queue with SYNC_QUEUED state */
        qop_p->op.state = OP_SYNC_QUEUED;
        dbpf_op_queue_add(sync_context->sync_queue, qop_p);
        sync_context->coalesce_counter++;
        ret = 0;
    }

    gen_mutex_unlock(sync_context->mutex);
    return ret;
}

int dbpf_sync_coalesce_enqueue(dbpf_queued_op_t *qop_p)
{
    dbpf_sync_context_t * sync_context;

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: enqueue called\n");

    /* only perform check if operation does modifications that require sync */
    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
        return 0;
    }

    /* only perform check if operation sync coalesce is enabled */
    if(DBPF_OP_IS_KEYVAL(qop_p->op.type) &&
       (qop_p->op.flags & TROVE_KEYVAL_SYNC_COALESCE))
    {
        sync_context = keyval_sync_array[qop_p->op.context_id];
    }
    else if(DBPF_OP_IS_DSPACE(qop_p->op.type) &&
            (qop_p->op.flags & TROVE_DSPACE_SYNC_COALESCE))
    {
        sync_context = dspace_sync_array[qop_p->op.context_id];
    }
    else
    {
        return 0;
    }

    gen_mutex_lock(sync_context->mutex);
    sync_context->sync_counter++;
    if(sync_context->sync_counter >= s_low_watermark)
    {
        /* mark this queued operation as able 
         * to be coalesced when syncing */
        qop_p->stats.coalesce_sync = 1;
    }

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: enqueue keyval counter: %d\n",
                 sync_context->sync_counter);

    gen_mutex_unlock(sync_context->mutex);

    return 0;
}

int dbpf_sync_coalesce_dequeue(
    dbpf_queued_op_t *qop_p)
{
    dbpf_sync_context_t * sync_context;
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dequeue called\n");

    /* only perform check if operation does modifications that require sync */
    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
        return 0;
    }

    /* only perform check if operation sync coalesce is enabled */
    if(DBPF_OP_IS_KEYVAL(qop_p->op.type) &&
       (qop_p->op.flags & TROVE_KEYVAL_SYNC_COALESCE))
    {
        sync_context = keyval_sync_array[qop_p->op.context_id];
    }
    else if(DBPF_OP_IS_DSPACE(qop_p->op.type) &&
       (qop_p->op.flags & TROVE_DSPACE_SYNC_COALESCE))
    {
        sync_context = dspace_sync_array[qop_p->op.context_id];
    }
    else
    {
        return 0;
    }

    gen_mutex_lock(sync_context->mutex);
    sync_context->sync_counter--;

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dequeue keyval counter: %d\n",
                 sync_context->sync_counter);

    gen_mutex_unlock(sync_context->mutex);

    return 0;
}

void dbpf_queued_op_set_sync_high_watermark(int high)
{
    s_high_watermark = high;
}

void dbpf_queued_op_set_sync_low_watermark(int low)
{
    s_low_watermark = low;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */ 
