/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dbpf-op-queue.h"
#include "gossip.h"

/* the queue that stores pending serviceable operations */
QLIST_HEAD(dbpf_op_queue);

/* lock to be obtained before manipulating dbpf_op_queue */
gen_mutex_t dbpf_op_queue_mutex = GEN_MUTEX_INITIALIZER;

#ifdef __PVFS2_TROVE_THREADED__
extern pthread_cond_t dbpf_op_incoming_cond;
#endif

/* dbpf_queued_op_put_and_dequeue()
 *
 * Assumption: we already have gotten responsibility for the op by
 * calling dbpf_queued_op_try_get() and succeeding.  This means that
 * the op will be in the OP_IN_SERVICE state.
 *
 * Remove the structure from the queue:
 * 1) lock the op queue
 * 2) remove op from queue
 * 3) unlock the op queue
 * 4) mark op as dequeued
 *
 * Note: this leaves open the possibility that someone might somehow
 * get a pointer to this operation and try to look at the state while
 * it is transitioning from OP_IN_SERVICE to OP_DEQUEUED.  However,
 * in order to do that they would need to get that pointer before we
 * lock the queue and use it afterwards, which is erroneous.  So we're
 * not going to try to protect against that.
 */
void dbpf_queued_op_put_and_dequeue(dbpf_queued_op_t *q_op_p)
{
    gen_mutex_lock(&dbpf_op_queue_mutex);
    assert((q_op_p->op.state == OP_IN_SERVICE) ||
           (q_op_p->op.state == OP_COMPLETED));
    dbpf_op_queue_remove(q_op_p);
    gen_mutex_unlock(&dbpf_op_queue_mutex);
    q_op_p->op.state = OP_DEQUEUED;
}

dbpf_op_queue_p dbpf_op_queue_new(void)
{
    struct qlist_head *tmp_queue = NULL;

    tmp_queue = (struct qlist_head *)malloc(sizeof(struct qlist_head));
    if (tmp_queue)
    {
        INIT_QLIST_HEAD(tmp_queue);
    }
    return tmp_queue;
}

void dbpf_op_queue_cleanup(dbpf_op_queue_p op_queue)
{
    dbpf_queued_op_t *cur_op = NULL;

    assert(op_queue);
    do
    {
        gen_mutex_lock(&dbpf_op_queue_mutex);
        cur_op = dbpf_op_queue_shownext(op_queue);
        if (cur_op)
        {
            dbpf_op_queue_remove(cur_op);
        }
        gen_mutex_unlock(&dbpf_op_queue_mutex);

    } while (cur_op);

    free(op_queue);
    op_queue = NULL;
    return;
}

void dbpf_op_queue_add(dbpf_op_queue_p op_queue,
                       dbpf_queued_op_t *dbpf_op)
{
    qlist_add_tail(&dbpf_op->link, op_queue);
}

void dbpf_op_queue_remove(dbpf_queued_op_t *dbpf_op)
{
    qlist_del(&dbpf_op->link);
}

int dbpf_op_queue_empty(dbpf_op_queue_p op_queue)
{
    return qlist_empty(op_queue);
}

dbpf_queued_op_t *dbpf_op_queue_shownext(dbpf_op_queue_p op_queue)
{
    dbpf_queued_op_t *next = NULL;
    if (op_queue->next != op_queue)
    {
        next = qlist_entry(op_queue->next, dbpf_queued_op_t, link);
    }
    return next;
}

/* dbpf_queued_op_try_get()
 *
 * Given an op_id, we look up the queued op structure.  We then determine
 * if the op is already in service (being worked on by another thread).
 * If it isn't, we mark it as in service and return it.
 *
 * We're letting the caller have direct access to the queued op structure for
 * performance reasons.  They shouldn't be mucking with the state or the
 * next/prev pointers.  Perhaps some structure reorganization is in order to
 * make that more clear?
 *
 * Returns one of DBPF_QUEUED_OP_INVALID, DBPF_QUEUED_OP_BUSY, or DBPF_QUEUED_OP_SUCCESS.
 */
int dbpf_queued_op_try_get(
    TROVE_op_id id,
    dbpf_queued_op_t **q_op_pp)
{
    int state;
    dbpf_queued_op_t *q_op_p = NULL;

    q_op_p = id_gen_fast_lookup(id);

    if (!q_op_p)
    {
        return DBPF_QUEUED_OP_INVALID;
    }

    /* NOTE: all we really need is atomic read/write to the state variable. */
    gen_mutex_lock(&q_op_p->mutex);
    state = q_op_p->op.state;
    if (state == OP_QUEUED) q_op_p->op.state = OP_IN_SERVICE;
    gen_mutex_unlock(&q_op_p->mutex);

    assert(state == OP_QUEUED || state == OP_COMPLETED);

    if (state == OP_QUEUED) {
	*q_op_pp = q_op_p;
	return DBPF_QUEUED_OP_SUCCESS;
    }
    else return DBPF_QUEUED_OP_BUSY;
}

/* dbpf_queued_op_put()
 *
 * Given a q_op_p, we lock the queued op.  If "completed" is nonzero, we mark the
 * state of the operation as completed.  Otherwise we mark it as queued.
 */
void dbpf_queued_op_put(dbpf_queued_op_t *q_op_p, int completed)
{
    gen_mutex_lock(&q_op_p->mutex);
    /* if op is already completed, never put it back to queued state */
    if (q_op_p->op.state != OP_COMPLETED)
    {
        q_op_p->op.state = (completed ? OP_COMPLETED : OP_QUEUED);
    }
    gen_mutex_unlock(&q_op_p->mutex);
}

/* dbpf_queued_op_queue()
 *
 * Gets the structure on the queue:
 * 1) lock the queue
 * 2) put the op into place
 * 3) unlock the queue
 * 4) return the id
 */
TROVE_op_id dbpf_queued_op_queue(dbpf_queued_op_t *q_op_p)
{
    TROVE_op_id tmp_id = 0;

    gen_mutex_lock(&dbpf_op_queue_mutex);

    dbpf_op_queue_add(&dbpf_op_queue, q_op_p);

    gen_mutex_lock(&q_op_p->mutex);
    q_op_p->op.state = OP_QUEUED;
    tmp_id = q_op_p->op.id;
    gen_mutex_unlock(&q_op_p->mutex);

#ifdef __PVFS2_TROVE_THREADED__
    /*
      wake up our operation thread if it's sleeping to let
      it know that a new op is available for servicing
    */
    pthread_cond_signal(&dbpf_op_incoming_cond);
#endif

    gen_mutex_unlock(&dbpf_op_queue_mutex);
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
void dbpf_queued_op_dequeue(dbpf_queued_op_t *q_op_p)
{
    gen_mutex_lock(&dbpf_op_queue_mutex);
    gen_mutex_lock(&q_op_p->mutex);

    assert(q_op_p->op.state != OP_DEQUEUED);
    assert(q_op_p->op.state != OP_IN_SERVICE);

    dbpf_op_queue_remove(q_op_p);
    q_op_p->op.state = OP_DEQUEUED;

    gen_mutex_unlock(&dbpf_op_queue_mutex);
    gen_mutex_unlock(&q_op_p->mutex);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
