/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_OP_QUEUE_H__
#define __DBPF_OP_QUEUE_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <trove.h>
#include <dbpf.h>
#include <string.h>
#include <assert.h>

#include "id-generator.h"

/* DBPF OPERATION QUEUE
 *
 * The purpose of the operation queue is to store the operations
 * in service.
 *
 * Interface
 * ---------
 * dbpf_queued_op_alloc() - allocate space for a dbpf op.
 * dbpf_queued_op_init() - initializes a "dbpf_queued_op" prior to
 *   insertion in the queue.
 * dbpf_queued_op_queue() - enqueues a previously initialized op.
 * dbpf_queued_op_dequeue() - removes a previously queued op.
 * dbpf_queued_op_free() - free space used for a previously allocated
 *   op.
 *
 * [ plus more ]
 *
 * Some guidelines for use:
 * - get an op before working on it, put it (or put_and_dequeue) when
 *   done
 * - for traversals of the queue, note that someone can come in and
 *   remove an operation, in any state, at any time, as long as they
 *   can get ahold of the queue lock.  they shouldn't do this to an
 *   op in service by someone else, however.
 * - service routines should not modify the op state.  they return
 *   something indicating completion, and the higher level call can
 *   do a put and set the complete flag instead.
 */

extern gen_mutex_t dbpf_op_queue_mutex;

/* struct dbpf_queued_op_stats
 *
 * used to maintain any desired statistics on the queued operation;
 * how many times we have worked on it, when it was queued, whatever.
 *
 * TODO: maybe merge these values into the main structure later?
 */
struct dbpf_queued_op_stats {
    int svc_ct;
};

/* struct dbpf_queued_op
 *
 * used to maintain an in-memory account of operations that have been
 * queued and not yet tested as complete
 *
 * dbpf_op_queue_head, defined in helper.c at the moment, is starting
 * point for queue.
 */
struct dbpf_queued_op {
    gen_mutex_t mutex;
    struct dbpf_op op;
    struct dbpf_queued_op_stats stats;
    struct dbpf_queued_op *next_p;
    struct dbpf_queued_op *prev_p;
};

extern struct dbpf_queued_op *dbpf_op_queue_head;

void dbpf_queued_op_put_and_dequeue(struct dbpf_queued_op *q_op_p);

/* dbpf_queued_op_init()
 *
 * Initializes a dbpf_queued_op structure.  Afterwards the op union and
 * the next_p must still be handled.
 *
 */
static inline void dbpf_queued_op_init(struct dbpf_queued_op *q_op_p,
				       enum dbpf_op_type type,
				       TROVE_handle handle,
				       struct dbpf_collection *coll_p,
				       int (*svc_fn)(struct dbpf_op *op),
				       void *user_ptr,
				       TROVE_ds_flags flags);

static inline void dbpf_queued_op_init(struct dbpf_queued_op *q_op_p,
				       enum dbpf_op_type type,
				       TROVE_handle handle,
				       struct dbpf_collection *coll_p,
				       int (*svc_fn)(struct dbpf_op *op),
				       void *user_ptr,
				       TROVE_ds_flags flags)
{
    memset(q_op_p, 0, sizeof(struct dbpf_queued_op));

    gen_mutex_init(&q_op_p->mutex);
    q_op_p->op.type     = type;
    q_op_p->op.state    = OP_NOT_QUEUED;
    q_op_p->op.handle   = handle;
    q_op_p->op.id       = 0; /* filled in when queued */
    q_op_p->op.coll_p   = coll_p;
    q_op_p->op.svc_fn   = svc_fn;
    q_op_p->op.user_ptr = user_ptr;
    q_op_p->op.flags    = flags;
}


enum {
    DBPF_QUEUED_OP_INVALID = -1,
    DBPF_QUEUED_OP_BUSY = 0,
    DBPF_QUEUED_OP_SUCCESS = 1
};

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
static inline int dbpf_queued_op_try_get(TROVE_op_id id, struct dbpf_queued_op **q_op_pp);

static inline int dbpf_queued_op_try_get(TROVE_op_id id,
					 struct dbpf_queued_op **q_op_pp)
{
    int state;
    struct dbpf_queued_op *q_op_p;

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
static inline void dbpf_queued_op_put(struct dbpf_queued_op *q_op_p, int completed);

static inline void dbpf_queued_op_put(struct dbpf_queued_op *q_op_p, int completed)
{
    int state;
    gen_mutex_lock(&q_op_p->mutex);
    state = q_op_p->op.state;
    if (completed) q_op_p->op.state = OP_COMPLETED;
    else q_op_p->op.state = OP_QUEUED;
    gen_mutex_unlock(&q_op_p->mutex);

    assert(state == OP_IN_SERVICE);
}


/* dbpf_queued_op_queue()
 *
 * Gets the structure on the queue:
 * 1) lock the queue
 * 2) put the op into place
 * 3) unlock the queue
 * 4) return the id
 */
static inline TROVE_op_id dbpf_queued_op_queue(struct dbpf_queued_op *q_op_p);

static inline TROVE_op_id dbpf_queued_op_queue(struct dbpf_queued_op *q_op_p)
{
    TROVE_op_id id;
    struct dbpf_queued_op *old_head_p;
    struct dbpf_queued_op *old_tail_p;

    gen_mutex_lock(&dbpf_op_queue_mutex);

    old_head_p = dbpf_op_queue_head;
    if (old_head_p == NULL) {
	/* previously empty queue */
	q_op_p->prev_p     = q_op_p;
	q_op_p->next_p     = q_op_p;
	dbpf_op_queue_head = q_op_p;
    }
    else {
	old_tail_p         = old_head_p->prev_p;
	old_head_p->prev_p = q_op_p;
	old_tail_p->next_p = q_op_p;
	q_op_p->next_p     = old_head_p;
	q_op_p->prev_p     = old_tail_p;
	dbpf_op_queue_head = q_op_p;
    }

    q_op_p->op.state = OP_QUEUED;

    gen_mutex_unlock(&dbpf_op_queue_mutex);

    /* drop in the id */
    id_gen_fast_register(&id, q_op_p);
    q_op_p->op.id = id;
    return q_op_p->op.id;
}

/* dbpf_queued_op_dequeue()
 *
 * Remove the structure from the queue:
 * 1) lock the queue
 * 2) lock the op, verify it's not in use (are 1 & 2 in the best order?)
 * 3) update all the pointers, mark the op as dequeued
 * 4) unlock the queue
 * 5) unlock the op
 */
static inline void dbpf_queued_op_dequeue(struct dbpf_queued_op *q_op_p);

static inline void dbpf_queued_op_dequeue(struct dbpf_queued_op *q_op_p)
{
    int state;

    gen_mutex_lock(&dbpf_op_queue_mutex);

    gen_mutex_lock(&q_op_p->mutex);
    state = q_op_p->op.state;

    assert(state != OP_DEQUEUED && state != OP_IN_SERVICE);

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

    q_op_p->next_p   = NULL;
    q_op_p->prev_p   = NULL;
    q_op_p->op.state = OP_DEQUEUED;

    gen_mutex_unlock(&dbpf_op_queue_mutex);
    gen_mutex_unlock(&q_op_p->mutex);
}

/* dbpf_queued_op_touch()
 *
 * Notes in statistics that we have been working on this operation
 * again.
 */
static inline void dbpf_queued_op_touch(struct dbpf_queued_op *q_op_p);

static inline void dbpf_queued_op_touch(struct dbpf_queued_op *q_op_p)
{
    q_op_p->stats.svc_ct++;
}

/* other functions defined in queue.c */
struct dbpf_queued_op *dbpf_queued_op_alloc(void);
void dbpf_queued_op_free(struct dbpf_queued_op *q_op_p);
void dbpf_queue_list(void);



#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
