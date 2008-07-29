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

static dbpf_txn_context_t txn_array[TROVE_MAX_CONTEXTS];

extern dbpf_op_queue_p dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];
extern pthread_cond_t dbpf_op_completed_cond;

static int dbpf_sync_db(
    DB * dbp, 
    enum s_sync_context_e sync_context_type, 
    dbpf_sync_context_t * sync_context)
{
    int ret; 
    DB_ENV *dbenv;
    u_int32_t flagsp;

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]:\tcoalesce %d sync start "
                 "in coalesce_queue:%d pending:%d\n",
                 sync_context_type, sync_context->coalesce_counter, 
                 sync_context->sync_counter);

    /*use checkpointing instead of sync for transactional db*/
    dbenv = dbp->get_env(dbp);
    if(dbenv != NULL)
    {
	dbenv->get_open_flags(dbenv, &flagsp);
	if(flagsp & DB_INIT_TXN)
	{
	    ret = dbenv->txn_checkpoint(dbenv, 0, 0, DB_FORCE);
	}
	else
	{
	    ret = dbp->sync(dbp, 0);
	}
	    
    }
    else
    {
	ret = dbp->sync(dbp, 0);
    }

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
	(*outcount)++;
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
                     "[SYNC_COALESCE]: moving op %p with handle: %llu "
                     "to completion queue\n",
                     qop_p, llu(qop_p->op.handle));

        DBPF_COMPLETION_START(qop_p, OP_COMPLETED);
        (*outcount)++;


        /* move remaining ops in queue with ready-to-be-synced state
         * to completion queue
         */
        while(!dbpf_op_queue_empty(sync_context->sync_queue))
        {
            ready_op = dbpf_op_queue_shownext(sync_context->sync_queue);

            gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                         "[SYNC_COALESCE]: moving op: %p with handle: %llu "
                         "to completion queue\n",
                         ready_op, llu(ready_op->op.handle));

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

void dbpf_db_replication_start(int is_master, struct dbpf_collection *coll)
{
    DB_ENV *dbenv = coll->coll_env;
    /* TODO: configurable priorities, nsites, timeout for replication group?
     * should check the return value....
     */
    dbenv->rep_set_transport(dbenv, 100/*self eid*/, PVFS_db_rep_send);
    if(is_master)
    {
	dbenv->rep_set_priority(dbenv, 100);
	dbenv->rep_start(dbenv, NULL, DB_REP_MASTER);
    }
    else
    {
	dbenv->rep_set_priority(dbenv, 90);
	dbenv->rep_start(dbenv, NULL, DB_REP_CLIENT);
    }
}

static int dbpf_txn_db(dbpf_txn_context_t *txn_context)
{
    int ret;
    DB_ENV *dbenv;
    DB_TXN *txn;
    dbpf_txn_entry_t *entry, *next;
    int retry_count = 0;

    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]:\tcoalesce txn start "
                 "in coalesce_queue:%d pending:%d\n",
                 txn_context->coalesce_counter, 
                 txn_context->sync_counter);
    entry = qlist_entry(txn_context->txn_queue->next, dbpf_txn_entry_t, link);
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: entry: %p\t entry->dbp: %p\n",
                 entry, entry->dbp);
    if(!entry->dbp)
    {
	return 0;
    }
    dbenv = entry->dbp->get_env(entry->dbp);
    assert(dbenv);
retry:
    txn = NULL;
    ret = dbenv->txn_begin(dbenv, NULL, &txn, 0);
    if(ret != 0)
    {
	gossip_err("db transaction failed: can't begin transaction: %s\n",
		   db_strerror(ret));
	ret = -dbpf_db_error_to_trove_error(ret);
	return ret;
    }
    qlist_for_each_entry_safe(entry, next, txn_context->txn_queue, link)
    {
	ret = entry->dbp->put(entry->dbp, txn, &(entry->key), &(entry->data), 0);
	if(ret != 0)
	{
	   gossip_err("db transaction failed: db put failed: %s\n",
		   db_strerror(ret)); 
	   txn->abort(txn);
	   if(retry_count < 4)
	   {
	       gossip_err("transaction aborted and retry %d\n", ++retry_count);
	       goto retry;
	   }
	   else
	   {
	       gossip_err("transaction aborted, no more retry\n");	       
	       ret = -dbpf_db_error_to_trove_error(ret);
	       return ret;
	   }
	}
    }
    ret = txn->commit(txn, 0);
    if(ret != 0)
    {
	gossip_err("db transaction failed: commit failed: %s\n",
		   db_strerror(ret));
	if(retry_count < 4)
	{
	    gossip_err("transaction aborted and retry %d\n", ++retry_count);
	    goto retry;
	}
	else
	{
	    gossip_err("transaction aborted, no more retry\n");
	    ret = -dbpf_db_error_to_trove_error(ret);
	    return ret;
	}
    }
    qlist_for_each_entry_safe(entry, next, txn_context->txn_queue, link)
    {
	qlist_del(&entry->link);
	free(entry);
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]:\tcoalesce txn stop\n");
    return 0;
}

int dbpf_txn_context_init(int context_index)
{
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dbpf_txn_context_init for "
                 "context %d called\n",
                 context_index);

    bzero(&txn_array[context_index], sizeof(dbpf_txn_context_t));
    gen_mutex_init(&(txn_array[context_index].mutex));
    txn_array[context_index].txn_queue = 
	(struct qlist_head*)malloc(sizeof(struct qlist_head));
    INIT_QLIST_HEAD(txn_array[context_index].txn_queue);
    txn_array[context_index].op_queue = dbpf_op_queue_new();
    return 0;
}

void dbpf_txn_context_destroy(int context_index)
{
    dbpf_txn_entry_t *p1, *p2;
    struct qlist_head *txn_queue = txn_array[context_index].txn_queue;
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: dbpf_txn_context_destroy for "
                 "context %d called\n",
                 context_index);	
    gen_mutex_lock(&(txn_array[context_index].mutex));
    assert(txn_queue);
    qlist_for_each_entry_safe(p1, p2, txn_queue, link)
    {
	qlist_del(&p1->link);
	free(p1);
    }
    free(txn_queue);
    gen_mutex_destroy(&txn_array[context_index].mutex);
    dbpf_op_queue_cleanup(txn_array[context_index].op_queue);
    return;
}

int dbpf_txn_coalesce(dbpf_queued_op_t *qop_p, int retcode, int *outcount)
{
    int ret = 0;
    dbpf_txn_context_t *txn_context;
    dbpf_queued_op_t *ready_op;
    struct dbpf_collection * coll = qop_p->op.coll_p;
    int cid = qop_p->op.context_id;

    qop_p->state = retcode;

    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
	dbpf_queued_op_complete(qop_p, OP_COMPLETED);
	(*outcount)++;
	return 0;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: txn_coalesce called, "
                 "handle: %llu, cid: %d\n",
                 llu(qop_p->op.handle), cid);

    if(!(qop_p->op.flags & TROVE_SYNC))
    {
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: txn not needed, "
                     "moving to completion queue: %llu\n",
                     llu(qop_p->op.handle));
        dbpf_queued_op_complete(qop_p, OP_COMPLETED);
	(*outcount)++;
        return 0;
    }

    txn_context = &txn_array[cid];
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: txn_context: %p\n", txn_context);    

    if(!coll->meta_sync_enabled)
    {
	int do_txn = 0;
        gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: meta sync disabled, "
                     "moving operation to completion queue\n");

        ret = dbpf_queued_op_complete(qop_p, OP_COMPLETED);

	gen_mutex_lock(&txn_context->mutex);
	txn_context->coalesce_counter++;
	if((coll->c_high_watermark > 0 && 
	    txn_context->coalesce_counter >= coll->c_high_watermark)
	   ||(txn_context->sync_counter < coll->c_low_watermark))
	{
            gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                         "[SYNC_COALESCE]:\thigh or low watermark reached:\n"
                         "\t\tcoalesced: %d\n\t\tqueued: %d\n",
                         txn_context->coalesce_counter,
                         txn_context->sync_counter);

            txn_context->coalesce_counter = 0;
            do_txn = 1;      				
	}
	gen_mutex_unlock(&txn_context->mutex);
	if(do_txn)
	{
            gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                         "[SYNC_COALESCE]: start transaction now!\n");
            ret = dbpf_txn_db(txn_context);
	}
	return ret;
    }
    gen_mutex_lock(&txn_context->mutex);
    if((coll->c_high_watermark > 0 &&
	txn_context->coalesce_counter >= coll->c_high_watermark)
       ||(txn_context->sync_counter < coll->c_low_watermark))
    {
	gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
		     "[SYNC_COALESCE]:\thigh or low watermark reached:\n"
		     "\t\tcoalesced: %d\n\t\tqueued: %d\n",
		     txn_context->coalesce_counter,
		     txn_context->sync_counter);
	gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
		     "[SYNC_COALESCE]: start transaction now!\n");
	ret = dbpf_txn_db(txn_context);
	gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]: moving op %p with handle: %llu "
                     "to completion queue\n",
                     qop_p, llu(qop_p->op.handle));
	DBPF_COMPLETION_START(qop_p, OP_COMPLETED);
	(*outcount)++;
	while(!dbpf_op_queue_empty(txn_context->op_queue))
	{
	    ready_op = dbpf_op_queue_shownext(txn_context->op_queue);
	    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                         "[SYNC_COALESCE]: moving op: %p with handle: %llu "
                         "to completion queue\n",
                         ready_op, llu(ready_op->op.handle));
	    dbpf_op_queue_remove(ready_op);
	    DBPF_COMPLETION_ADD(ready_op, OP_COMPLETED);
	    (*outcount)++;
	}
	txn_context->coalesce_counter = 0;
	DBPF_COMPLETION_SIGNAL();
	DBPF_COMPLETION_FINISH(cid);
	ret = 1;
    }
    else
    {
	gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                     "[SYNC_COALESCE]:\tcoalescing type: transaction "
                     "coalesce_counter: %d, sync_counter: %d, handle %llu\n", 
                     txn_context->coalesce_counter,
                     txn_context->sync_counter,
                     llu(qop_p->op.handle));
	dbpf_op_queue_add(txn_context->op_queue, qop_p);
	txn_context->coalesce_counter++;
	ret = 0;
    }
    gen_mutex_unlock(&txn_context->mutex);
    return ret;
}

int dbpf_txn_coalesce_dequeue(dbpf_queued_op_t *qop_p)
{
    dbpf_txn_context_t *txn_context;

    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
	return 0;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: txn dequeue called\n");
    txn_context = &txn_array[qop_p->op.context_id];
    gen_mutex_lock(&txn_context->mutex);
    if(qop_p->op.flags & TROVE_SYNC)
    {
	txn_context->sync_counter--;
    }
    else
    {
	txn_context->non_sync_counter--;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: txn dequeue: counter sync:%d non_sync:%d\n",
                 txn_context->sync_counter, txn_context->non_sync_counter);

    gen_mutex_unlock(&txn_context->mutex);

    return 0;
}

int dbpf_txn_coalesce_enqueue(dbpf_queued_op_t *qop_p)
{
    dbpf_txn_context_t *txn_context;

    if(!DBPF_OP_DOES_SYNC(qop_p->op.type))
    {
	return 0;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: txn enqueue called\n");

    txn_context = &txn_array[qop_p->op.context_id];

    gen_mutex_lock(&txn_context->mutex);

    if(qop_p->op.flags & TROVE_SYNC)
    {
	txn_context->sync_counter++;
    }
    else
    {
	txn_context->non_sync_counter++;
    }
    gossip_debug(GOSSIP_DBPF_COALESCE_DEBUG,
                 "[SYNC_COALESCE]: txn enqueue: counter sync:%d non_sync:%d\n",
                 txn_context->sync_counter, txn_context->non_sync_counter);

    gen_mutex_unlock(&txn_context->mutex);

    return 0;
}

int dbpf_txn_queue_add(TROVE_context_id context_index, DB *dbp, DBT *key, DBT *data)
{
    dbpf_txn_entry_t *txn_entry;
    char *keybuf, *databuf;

    txn_entry = (dbpf_txn_entry_t *)malloc(sizeof(dbpf_txn_entry_t));
    keybuf = (char *)malloc(key->size);
    databuf = (char *)malloc(data->size);
    memcpy(keybuf, key->data, key->size);
    memcpy(databuf, data->data, data->size);
    txn_entry->dbp = dbp;
    memset(&(txn_entry->key), 0, sizeof(DBT));
    memset(&(txn_entry->data), 0, sizeof(DBT));
    txn_entry->key.size = key->size;
    txn_entry->key.data = keybuf;
    txn_entry->data.size = data->size;
    txn_entry->data.data = databuf;
 
    qlist_add(&(txn_entry->link), txn_array[context_index].txn_queue);
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */ 
