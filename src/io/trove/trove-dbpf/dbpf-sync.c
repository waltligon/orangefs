/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dbpf-op-queue.h"
#include "gossip.h"
#include "pint-perf-counter.h"
#include "dbpf-sync.h"
#include "dbpf-thread.h"

enum s_sync_context_e
{
    COALESCE_CONTEXT_KEYVAL = 0,
    COALESCE_CONTEXT_DSPACE = 1,
    COALESCE_CONTEXT_LAST = 2
};

static dbpf_sync_context_t 
    sync_array[COALESCE_CONTEXT_LAST][TROVE_MAX_CONTEXTS];

extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];
extern pthread_cond_t dbpf_op_completed_cond;

static int dbpf_sync_db(
    DB * dbp, 
    enum s_sync_context_e sync_context_type, 
    dbpf_sync_context_t * sync_context)
{
    int ret; 
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]:\tcoalesce %d sync start "
                 "in coalesce_queue:%d pending:%d\n",
                 sync_context_type, sync_context->coalesce_counter, 
                 sync_context->sync_counter);
    ret = dbp->sync(dbp, 0);
    if(ret != 0)
    {
        gossip_err("db SYNC failed: %s\n",
                   db_strerror(ret));
        ret = -dbpf_db_error_to_trove_error(ret);
        return ret;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]:\tcoalesce %d sync stop\n",
                 sync_context_type);
    return 0;
}

static int dbpf_sync_get_object_sync_context(enum dbpf_op_type type)
{
    assert(DBPF_OP_IS_KEYVAL(type) || DBPF_OP_IS_DSPACE(type));

    if(DBPF_OP_IS_KEYVAL(type))
    {
        return COALESCE_CONTEXT_KEYVAL;
    }
    else
    {
        return COALESCE_CONTEXT_DSPACE;
    }
}

int dbpf_sync_context_init(int context_index)
{
    int c;
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dbpf_sync_context_init for "
                 "context %d called\n",
                 context_index);
    for(c=0; c < COALESCE_CONTEXT_LAST; c++)
    {
        bzero(& sync_array[c][context_index], sizeof(dbpf_sync_context_t));

        gen_mutex_init(&sync_array[c][context_index].mutex);
        sync_array[c][context_index].sync_queue = dbpf_op_queue_new();
    }

    return 0;
}

void dbpf_sync_context_destroy(int context_index)
{
    int c;
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dbpf_sync_context_destroy for "
                 "context %d called\n",
                 context_index);	
    for(c=0; c < COALESCE_CONTEXT_LAST; c++)
    {
        gen_mutex_lock(&sync_array[c][context_index].mutex);
        gen_mutex_destroy(&sync_array[c][context_index].mutex);
        dbpf_op_queue_cleanup(sync_array[c][context_index].sync_queue);
    }
}

int dbpf_sync_coalesce(dbpf_queued_op_t *qop_p, int retcode, int * outcount)
{

    int ret = 0;
    DB * dbp = NULL;
    dbpf_sync_context_t * sync_context;
    dbpf_queued_op_t *ready_op;
    int sync_context_type;
    struct dbpf_collection* coll = qop_p->op.coll_p;
    int cid = qop_p->op.context_id;

    /* We want to set the state in all cases
     */
    qop_p->state = retcode;

    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
        dbpf_queued_op_complete(qop_p, OP_COMPLETED);
        (*outcount)++;
        return 0;
    }

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: sync_coalesce called, "
                 "handle: %llu, cid: %d\n",
                 llu(qop_p->op.handle), cid);

    sync_context_type = dbpf_sync_get_object_sync_context(qop_p->op.type);

    if ( ! (qop_p->op.flags & TROVE_SYNC) ) {
        /*
         * No sync needed at all
         */
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: sync not needed, "
                     "moving to completion queue: %llu\n",
                     llu(qop_p->op.handle));
        dbpf_queued_op_complete(qop_p, OP_COMPLETED);
        return 0;
    }

    /*
     * Now we know that this particular op is modifying
     */
    sync_context = & sync_array[sync_context_type][cid];

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: sync_context: %p\n", sync_context);

    if( sync_context_type == COALESCE_CONTEXT_DSPACE )
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: sync context type is DSPACE\n");
        dbp = qop_p->op.coll_p->ds_db;
    }
    else if( sync_context_type == COALESCE_CONTEXT_KEYVAL ) 
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: sync context type is KEYVAL\n");
        dbp = qop_p->op.coll_p->keyval_db;
    }

    if ( ! coll->meta_sync_enabled )
    {
        int do_sync=0;
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: meta sync disabled, "
                     "moving operation to completion queue\n");

        ret = dbpf_queued_op_complete(qop_p, OP_COMPLETED);

        /*
         * Sync periodical if count < lw or if lw = 0 and count > hw 
         */
        gen_mutex_lock(&sync_context->mutex);
        sync_context->coalesce_counter++;
        if( (coll->c_high_watermark > 0 && 
             sync_context->coalesce_counter >= coll->c_high_watermark) 
            || sync_context->sync_counter < coll->c_low_watermark )
        {
            gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                         "[SYNC_COALESCE]:\thigh or low watermark reached:\n"
                         "\t\tcoalesced: %d\n\t\tqueued: %d\n",
                         sync_context->coalesce_counter,
                         sync_context->sync_counter);

            sync_context->coalesce_counter = 0;
            do_sync = 1;      				
        }
        gen_mutex_unlock(&sync_context->mutex);

        if ( do_sync ) {
            gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                         "[SYNC_COALESCE]: syncing now!\n");
            ret = dbpf_sync_db(dbp, sync_context_type, sync_context);
        }

        return ret;
    }

    /*
     * metadata sync is enabled, either we delay and enqueue this op or we 
     * coalesce. 
     */
    gen_mutex_lock(&sync_context->mutex);
    if( (sync_context->sync_counter < coll->c_low_watermark) ||
        ( coll->c_high_watermark > 0 && 
          sync_context->coalesce_counter >= coll->c_high_watermark ) )
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]:\thigh or low watermark reached:\n"
                     "\t\tcoalesced: %d\n\t\tqueued: %d\n",
                     sync_context->coalesce_counter,
                     sync_context->sync_counter);

        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: syncing now!\n");
        ret = dbpf_sync_db(dbp, sync_context_type, sync_context);

        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: moving op: %p, handle: %llu , type: %d "
                     "to completion queue\n",
                     qop_p, llu(qop_p->op.handle), qop_p->op.type);

        if(qop_p->event_type == trove_dbpf_dspace_create_event_id)
        {
            PINT_EVENT_END(qop_p->event_type, dbpf_pid, NULL, qop_p->event_id,
                           qop_p->op.u.d_create.out_handle_p);
        }
        else
        {
            PINT_EVENT_END(qop_p->event_type, dbpf_pid, NULL, qop_p->event_id);
        }

        DBPF_COMPLETION_START(qop_p, OP_COMPLETED);
        (*outcount)++;


        /* move remaining ops in queue with ready-to-be-synced state
         * to completion queue
         */
        while(!dbpf_op_queue_empty(sync_context->sync_queue))
        {
            ready_op = dbpf_op_queue_shownext(sync_context->sync_queue);

            if(ready_op->event_type == trove_dbpf_dspace_create_event_id)
            {
                PINT_EVENT_END(ready_op->event_type, dbpf_pid, NULL, ready_op->event_id,
                               ready_op->op.u.d_create.out_handle_p);
            }
            else
            {
                PINT_EVENT_END(ready_op->event_type, dbpf_pid, NULL, ready_op->event_id);
            }

            gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                         "[SYNC_COALESCE]: moving op: %p, handle: %llu , type: %d "
                         "to completion queue\n",
                         ready_op, llu(ready_op->op.handle), ready_op->op.type);

            dbpf_op_queue_remove(ready_op);
            DBPF_COMPLETION_ADD(ready_op, OP_COMPLETED);
            (*outcount)++;
        }

        sync_context->coalesce_counter = 0;
        DBPF_COMPLETION_SIGNAL();
        DBPF_COMPLETION_FINISH(cid);
        ret = 1;
    }
    else
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]:\tcoalescing type: %d "
                     "coalesce_counter: %d, sync_counter: %d, handle %llu\n", 
                     sync_context_type, sync_context->coalesce_counter,
                     sync_context->sync_counter,
                     llu(qop_p->op.handle));

        dbpf_op_queue_add(
            sync_context->sync_queue, qop_p);
        sync_context->coalesce_counter++;
        ret = 0;
    }

    gen_mutex_unlock(&sync_context->mutex);
    return ret;
}

int dbpf_sync_coalesce_enqueue(dbpf_queued_op_t *qop_p)
{
    dbpf_sync_context_t * sync_context;
    int sync_context_type;

    if (!DBPF_OP_DOES_SYNC(qop_p->op.type))
    { 
        return 0;
    } 

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: enqueue called\n");

    sync_context_type = dbpf_sync_get_object_sync_context(qop_p->op.type);

    sync_context = & sync_array[sync_context_type][qop_p->op.context_id];

    gen_mutex_lock(&sync_context->mutex);

    if( (qop_p->op.flags & TROVE_SYNC) )
    { 
        sync_context->sync_counter++;
    }
    else
    {
        sync_context->non_sync_counter++;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: enqueue %d counter sync:%d non_sync:%d\n",
                 sync_context_type,
                 sync_context->sync_counter, sync_context->non_sync_counter);

    gen_mutex_unlock(&sync_context->mutex);

    return 0;
}

int dbpf_sync_coalesce_dequeue(
    dbpf_queued_op_t *qop_p)
{
    dbpf_sync_context_t * sync_context;
    int sync_context_type;

    if (!DBPF_OP_DOES_SYNC(qop_p->op.type))
    { 
        return 0;
    } 

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dequeue called\n");

    sync_context_type = dbpf_sync_get_object_sync_context(qop_p->op.type);

    sync_context = & sync_array[sync_context_type][qop_p->op.context_id];

    gen_mutex_lock(&sync_context->mutex);
    if( (qop_p->op.flags & TROVE_SYNC) )
    { 
        sync_context->sync_counter--;
    }
    else
    {
        sync_context->non_sync_counter--;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dequeue %d counter sync:%d non_sync:%d\n",
                 sync_context_type,
                 sync_context->sync_counter, sync_context->non_sync_counter);

    gen_mutex_unlock(&sync_context->mutex);

    return 0;
}

void dbpf_queued_op_set_sync_high_watermark(
    int high, struct dbpf_collection* coll)
{
    coll->c_high_watermark = high;
}

void dbpf_queued_op_set_sync_low_watermark(
    int low, struct dbpf_collection* coll)
{
    coll->c_low_watermark = low;
}

void dbpf_queued_op_set_sync_mode(int enabled, struct dbpf_collection* coll)
{
    /*
     * Right now we don't have to check if there are operations queued in the
     * coalesync queue, because the mode is set on startup...
     */
    coll->meta_sync_enabled = enabled;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */ 
