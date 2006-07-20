/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dbpf-op-queue.h"
#include "gossip.h"
#include "pint-perf-counter.h"
#include "dbpf-sync.h"

/* the queue that stores pending serviceable operations */
dbpf_op_queue_s dbpf_op_queue[OP_QUEUE_LAST];

/* lock to be obtained before manipulating dbpf_op_queue */
gen_mutex_t     dbpf_op_queue_mutex[OP_QUEUE_LAST];

pthread_cond_t dbpf_op_completed_cond;
pthread_cond_t dbpf_op_incoming_cond[OP_QUEUE_LAST];

/*
static void debugPrintList(dbpf_op_queue_s *  op_queue){
       gossip_debug(GOSSIP_TROVE_DEBUG,
                 "op_queue %p Elems: %d\n",
                 op_queue, op_queue->elems);
}
*/

void dbpf_move_op_to_completion_queue(dbpf_queued_op_t *cur_op, 
    TROVE_ds_state ret_state, enum dbpf_op_state end_state){
    TROVE_context_id cid = cur_op->op.context_id;
    gen_mutex_t * context_mutex = dbpf_completion_queue_array_mutex[cid];
    dbpf_move_op_to_completion_queue_nolock(cur_op, ret_state, end_state);

    /* wake up one waiting thread, if any */
    pthread_cond_signal(&dbpf_op_completed_cond);
    gen_mutex_unlock(context_mutex);
}


void dbpf_move_op_to_completion_queue_nolock(dbpf_queued_op_t *cur_op, 
    TROVE_ds_state ret_state, enum dbpf_op_state end_state){
    
    TROVE_context_id cid = cur_op->op.context_id;
    assert( cur_op->op.state != OP_DEQUEUED );
    
    gossip_debug(GOSSIP_TROVE_DEBUG,
          "move_op_to_completion_queue type: %s handle:%llu - %p - "
          " ret_state: %d end_state:%d\n",
          dbpf_op_type_to_str(cur_op->op.type), llu(cur_op->op.handle),
          cur_op, ret_state, end_state);
    
    cur_op->state = ret_state;
    
    dbpf_op_change_status(cur_op, end_state);
    
    dbpf_op_queue_add(dbpf_completion_queue_array[cid],cur_op);

    gossip_debug(GOSSIP_PERFORMANCE_DEBUG,
        "DBPF move_op_to_completion_queue, queued elements in queue %d\n",
        dbpf_completion_queue_array[cid]->elems);
}

dbpf_op_queue_s * dbpf_op_queue_new(void)
{
    dbpf_op_queue_s *tmp_queue;

    tmp_queue = (dbpf_op_queue_s *)malloc(sizeof(dbpf_op_queue_s));
    if (tmp_queue)
        dbpf_op_queue_init(tmp_queue);
    return tmp_queue;
}

void dbpf_op_queue_init(dbpf_op_queue_s * queue)
{
   INIT_QLIST_HEAD(& queue->list);
   queue->elems = 0;
}


void dbpf_op_queue_cleanup_nolock(dbpf_op_queue_s * op_queue)
{
    dbpf_queued_op_t *cur_op = NULL;

    assert(op_queue);
    do
    {
        cur_op = dbpf_op_pop_front_nolock(op_queue);
    } while (cur_op);

    free(op_queue);
    op_queue = NULL;
    return;
}

void dbpf_op_queue_add(dbpf_op_queue_s *  op_queue,
                       dbpf_queued_op_t *dbpf_op)
{
    op_queue->elems++;
    dbpf_op->queue = op_queue;

    qlist_add_tail(&dbpf_op->link, & op_queue->list);
}

void dbpf_op_queue_remove(dbpf_queued_op_t *dbpf_op)
{
    dbpf_op_queue_s * queue = dbpf_op->queue;
    queue->elems--;
    assert(queue->elems > -1);

    qlist_del(&dbpf_op->link);
}

int dbpf_op_queue_empty(dbpf_op_queue_s *  op_queue)
{
    return qlist_empty(& op_queue->list);
}

dbpf_queued_op_t *dbpf_op_pop_front_nolock(dbpf_op_queue_s *  op_queue)
{
    if ( op_queue->elems == 0 ) return NULL;

    dbpf_queued_op_t * elem = qlist_entry( op_queue->list.next, dbpf_queued_op_t, link);
    
    gossip_debug(GOSSIP_TROVE_DEBUG,
                 "op_queue %p dbpf_op_pop_front_nolock elem: %p\n",
                 op_queue, elem);
    dbpf_op_queue_remove (elem);
    return elem;
}

/* dbpf_queued_op_queue()
 *
 * Gets the structure on the queue:
 * 1) lock the queue
 * 2) put the op into place
 * 3) unlock the queue
 * 4) return the id
 */
TROVE_op_id dbpf_queued_op_queue(dbpf_queued_op_t *q_op_p, dbpf_op_queue_s* queue)
{
    TROVE_op_id tmp_id = 0;
    assert(q_op_p->queue_type < OP_QUEUE_LAST);
    gossip_debug(GOSSIP_TROVE_DEBUG, 
        "dbpf_queued_op_queue queue new object %p to queue %d \n", 
        q_op_p, q_op_p->queue_type);

    gen_mutex_lock(&dbpf_op_queue_mutex[q_op_p->queue_type]);

    tmp_id = dbpf_queued_op_queue_nolock(q_op_p, queue);

    gen_mutex_unlock(&dbpf_op_queue_mutex[q_op_p->queue_type]);

    return tmp_id;
}

/* dbpf_queued_op_queue_nolock()
 *
 * same as dbpf_queued_op_queue(), but assumes dbpf_op_queue_mutex
 * already held
 */
TROVE_op_id dbpf_queued_op_queue_nolock(dbpf_queued_op_t *q_op_p, dbpf_op_queue_s* queue)
{
    TROVE_op_id tmp_id = 0;
    
    dbpf_op_queue_add(queue, q_op_p);
    
    dbpf_op_change_status(q_op_p, OP_QUEUED);
    tmp_id = q_op_p->op.id;
    
    dbpf_sync_coalesce_enqueue(q_op_p);

    /*
      wake up our operation thread if it's sleeping to let
      it know that a new op is available for servicing
    */
    pthread_cond_signal(& dbpf_op_incoming_cond[q_op_p->queue_type]);

    return tmp_id;
}

/* dbpf_queued_op_dequeue()
 *
 * Remove the structure from the queue:
 * 1) lock the queue
 * 2) lock the op, verify it's not in use
 * 3) update all the pointers, mark the op as dequeued
 * 4) unlock the queue
 * 5) unlock the op
 */
void dbpf_queued_op_dequeue(dbpf_queued_op_t *q_op_p )
{
    gen_mutex_lock(&dbpf_op_queue_mutex[q_op_p->queue_type]);

    dbpf_queued_op_dequeue_nolock(q_op_p);

    gen_mutex_unlock(&dbpf_op_queue_mutex[q_op_p->queue_type]);
}

void dbpf_queued_op_dequeue_nolock(dbpf_queued_op_t *q_op_p)
{
    assert(q_op_p->op.state != OP_DEQUEUED);
    assert(q_op_p->op.state != OP_IN_SERVICE);

    dbpf_op_change_status(q_op_p, OP_DEQUEUED);
    dbpf_op_queue_remove(q_op_p);
    dbpf_sync_coalesce_dequeue(q_op_p);
}

int dbpf_op_init_queued_or_immediate(
    struct dbpf_op * op_p,
    dbpf_queued_op_t ** q_op_pp,
    enum dbpf_op_type op_type,
    struct dbpf_collection *coll_p,
    TROVE_handle handle,
    int (* dbpf_op_svc_fn) (struct dbpf_op *),
    TROVE_ds_flags flags,
    TROVE_vtag_s *vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    struct dbpf_op **op_pp)
{
    if(coll_p->immediate_completion)
    {
        DBPF_OP_INIT(*op_p,
                      op_type,
                      OP_QUEUED,
                      handle,
                      coll_p,
                      dbpf_op_svc_fn,
                      user_ptr,
                      flags,
                      context_id,
                      0);
        *op_pp = op_p;
    }
    else
    {
        /* grab a queued op structure */
        *q_op_pp = dbpf_queued_op_alloc();
        if (*q_op_pp == NULL)
        {
            return -TROVE_ENOMEM;
        }

        /* initialize all the common members */
        dbpf_queued_op_init(*q_op_pp,
                            op_type,
                            handle,
                            coll_p,
                            dbpf_op_svc_fn,
                            user_ptr,
                            flags,
                            context_id
                            );
        *op_pp = &(*q_op_pp)->op;
    }

    return 0;
}

int dbpf_queue_or_service(
    struct dbpf_op *op_p,
    dbpf_queued_op_t *q_op_p,
    struct dbpf_collection *coll_p,
    TROVE_op_id *out_op_id_p)
{
    int ret;

    if( coll_p->immediate_completion &&
       (DBPF_OP_IS_KEYVAL(op_p->type) || DBPF_OP_IS_DSPACE(op_p->type)))
    {
        DB * dbp;
        *out_op_id_p = 0;
        ret = op_p->svc_fn(op_p);
        if(ret < 0)
        {
            goto exit;
        }

        if(DBPF_OP_IS_KEYVAL(op_p->type))
        {
            dbp = op_p->coll_p->keyval_db;
        }
        else
        {
            dbp = op_p->coll_p->ds_db;
        }

        DBPF_DB_SYNC_IF_NECESSARY(op_p, dbp, ret);
        if(ret < 0)
        {
            goto exit;
        }

        ret = 1;

    }
    else
    {
        *out_op_id_p = dbpf_queued_op_queue(q_op_p, & dbpf_op_queue[q_op_p->queue_type]);
        ret = 0;
    }

exit:

    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
