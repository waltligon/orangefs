/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <dbpf-op-queue.h>
#include <malloc.h>

/* dbpf_op_queue_mutex - lock to be obtained before manipulating queue
 */
gen_mutex_t dbpf_op_queue_mutex = GEN_MUTEX_INITIALIZER;

struct dbpf_queued_op *dbpf_op_queue_head = NULL;

/* Note: the majority of the operations on the queue are static inlines
 * and are defined in dbpf-op-queue.h for speed.
 */

struct dbpf_queued_op *dbpf_queued_op_alloc(void)
{
    struct dbpf_queued_op *q_op_p;

    q_op_p = (struct dbpf_queued_op *) malloc(sizeof(struct dbpf_queued_op));
    return q_op_p;
}

void dbpf_queued_op_free(struct dbpf_queued_op *q_op_p)
{
    if (q_op_p->op.type == DSPACE_CREATE)
    {
        free(q_op_p->op.u.d_create.extent_array.extent_array);
    }
    free(q_op_p);
}

void dbpf_queue_list(void)
{
    struct dbpf_queued_op *q_op, *start_op;

    gen_mutex_lock(&dbpf_op_queue_mutex);

    q_op = dbpf_op_queue_head;

    if (q_op == NULL) {
	printf("<queue empty>\n");
	gen_mutex_unlock(&dbpf_op_queue_mutex);
	return;
    }
    
    start_op = q_op;
    printf("op: id=%Lx, type=%d, state=%d, handle=%Lx\n",
	   q_op->op.id,
	   q_op->op.type,
	   q_op->op.state,
	   q_op->op.handle);
 
    q_op = q_op->next_p;
    while (q_op != start_op) {
	printf("op: id=%Lx, type=%d, state=%d, handle=%Lx\n",
	       q_op->op.id,
	       q_op->op.type,
	       q_op->op.state,
	       q_op->op.handle);
	q_op = q_op->next_p;
    }

    gen_mutex_unlock(&dbpf_op_queue_mutex);
}

/* dbpf_queued_op_put_and_dequeue()
 *
 * Assumption: we already have gotten responsibility for the op by
 * calling dbpf_queued_op_try_get() and succeeding.  This means that
 * the op will be in the OP_IN_SERVICE state.
 *
 * Remove the structure from the queue:
 * 1) lock the queue
 * 2) update all the pointers in queue
 * 3) unlock the queue
 * 4) update pointers in op, mark as dequeued
 *
 * Note: this leaves open the possibility that someone might somehow
 * get a pointer to this operation and try to look at the state while
 * it is transitioning from OP_IN_SERVICE to OP_DEQUEUED.  However,
 * in order to do that they would need to get that pointer before we
 * lock the queue and use it afterwards, which is erroneous.  So we're
 * not going to try to protect against that.
 */


void dbpf_queued_op_put_and_dequeue(struct dbpf_queued_op *q_op_p)
{
    int state;

    gen_mutex_lock(&dbpf_op_queue_mutex);

    state = q_op_p->op.state;

    assert(state == OP_IN_SERVICE || state == OP_COMPLETED);

    if ( (q_op_p->next_p == NULL) ||  (q_op_p->next_p == q_op_p)) {
	/* only one on list. ->next_p might have gotten set to NULL the
	 * last time through */
	dbpf_op_queue_head = NULL;
    }
    else {
	q_op_p->next_p->prev_p = q_op_p->prev_p;
	q_op_p->prev_p->next_p = q_op_p->next_p;

	if (dbpf_op_queue_head == q_op_p) {
	    /* this is the head */
	    dbpf_op_queue_head = q_op_p->next_p;
	}
    }

    gen_mutex_unlock(&dbpf_op_queue_mutex);

    q_op_p->next_p   = NULL;
    q_op_p->prev_p   = NULL;
    q_op_p->op.state = OP_DEQUEUED;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
