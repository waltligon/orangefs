/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dbpf-op-queue.h"
#include "malloc.h"

/* the queue that stores pending serviceable operations */
static QLIST_HEAD(dbpf_op_queue);

/* lock to be obtained before manipulating dbpf_op_queue */
gen_mutex_t dbpf_op_queue_mutex = GEN_MUTEX_INITIALIZER;

/* Note: the majority of the operations on the queue are static inlines
 * and are defined in dbpf-op-queue.h for speed.
 */
dbpf_queued_op_t *dbpf_queued_op_alloc(void)
{
    return (dbpf_queued_op_t *)malloc(sizeof(dbpf_queued_op_t));
}

/* dbpf_queued_op_init()
 *
v * Initializes a dbpf_queued_op_t structure.  Afterwards the op union and
 * the next_p must still be handled.
 *
 */
void dbpf_queued_op_init(
    dbpf_queued_op_t *q_op_p,
    enum dbpf_op_type type,
    TROVE_handle handle,
    struct dbpf_collection *coll_p,
    int (*svc_fn)(struct dbpf_op *op),
    void *user_ptr,
    TROVE_ds_flags flags,
    TROVE_context_id context_id)
{
    assert(q_op_p);
    memset(q_op_p, 0, sizeof(dbpf_queued_op_t));

    INIT_QLIST_HEAD(&q_op_p->link);
    gen_mutex_init(&q_op_p->mutex);
    q_op_p->op.type       = type;
    q_op_p->op.state      = OP_NOT_QUEUED;
    q_op_p->op.handle     = handle;
    q_op_p->op.id         = 0; /* filled in when queued */
    q_op_p->op.coll_p     = coll_p;
    q_op_p->op.svc_fn     = svc_fn;
    q_op_p->op.user_ptr   = user_ptr;
    q_op_p->op.flags      = flags;
    q_op_p->op.context_id = context_id;
}

void dbpf_queued_op_free(dbpf_queued_op_t *q_op_p)
{
    if (q_op_p->op.type == DSPACE_CREATE)
    {
        free(q_op_p->op.u.d_create.extent_array.extent_array);
    }
    free(q_op_p);
}

void dbpf_queue_list()
{
/*     dbpf_queued_op_t *q_op = NULL, *start_op = NULL; */

    gen_mutex_lock(&dbpf_op_queue_mutex);

/*     q_op = dbpf_op_queue_head; */

/*     if (q_op == NULL) { */
/* 	printf("<queue empty>\n"); */
/* 	gen_mutex_unlock(&dbpf_op_queue_mutex); */
/* 	return; */
/*     } */
    
/*     start_op = q_op; */
/*     printf("op: id=%Lx, type=%d, state=%d, handle=%Lx\n", */
/* 	   q_op->op.id, */
/* 	   q_op->op.type, */
/* 	   q_op->op.state, */
/* 	   q_op->op.handle); */
 
/*     q_op = q_op->next_p; */
/*     while (q_op != start_op) { */
/* 	printf("op: id=%Lx, type=%d, state=%d, handle=%Lx\n", */
/* 	       q_op->op.id, */
/* 	       q_op->op.type, */
/* 	       q_op->op.state, */
/* 	       q_op->op.handle); */
/* 	q_op = q_op->next_p; */
/*     } */

    gen_mutex_unlock(&dbpf_op_queue_mutex);
}

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
        cur_op = dbpf_op_queue_shownext(op_queue);
        if (cur_op)
        {
            dbpf_op_queue_remove(cur_op);
        }
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
    gen_mutex_lock(&dbpf_op_queue_mutex);
    dbpf_op_queue_add(&dbpf_op_queue, q_op_p);
    q_op_p->op.state = OP_QUEUED;
    gen_mutex_unlock(&dbpf_op_queue_mutex);

    id_gen_fast_register(&q_op_p->op.id, q_op_p);
    return q_op_p->op.id;
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

/* dbpf_queued_op_touch()
 *
 * Notes in statistics that we have been working on this operation
 * again.
 */
void dbpf_queued_op_touch(dbpf_queued_op_t *q_op_p)
{
    q_op_p->stats.svc_ct++;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
