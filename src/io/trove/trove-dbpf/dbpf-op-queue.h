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

/* dbpf_queued_op_init()
 *
 * Initializes a dbpf_queued_op structure.  Afterwards the op union and
 * the next_p must still be handled.
 */
static inline void dbpf_queued_op_init(struct dbpf_queued_op *q_op_p,
				       enum dbpf_op_type type,
				       TROVE_handle handle,
				       TROVE_op_id id,
				       struct dbpf_collection *coll_p,
				       int (*svc_fn)(struct dbpf_op *op),
				       void *user_ptr);

static inline void dbpf_queued_op_init(struct dbpf_queued_op *q_op_p,
				       enum dbpf_op_type type,
				       TROVE_handle handle,
				       TROVE_op_id id,
				       struct dbpf_collection *coll_p,
				       int (*svc_fn)(struct dbpf_op *op),
				       void *user_ptr)
{
    memset(q_op_p, 0, sizeof(struct dbpf_queued_op));

    gen_mutex_init(&q_op_p->mutex);
    q_op_p->op.type     = type;
    q_op_p->op.state    = OP_NOT_QUEUED;
    q_op_p->op.handle   = handle;
    q_op_p->op.id       = id;
    q_op_p->op.coll_p   = coll_p;
    q_op_p->op.svc_fn   = svc_fn;
    q_op_p->op.user_ptr = user_ptr;
}

/* dbpf_queued_op_queue()
 *
 * Gets the structure on the queue.
 */
static inline void dbpf_queued_op_queue(struct dbpf_queued_op *q_op_p);

static inline void dbpf_queued_op_queue(struct dbpf_queued_op *q_op_p)
{
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
}

/* dbpf_queued_op_dequeue()
 *
 * Remove the structure from the queue.
 */
static inline void dbpf_queued_op_dequeue(struct dbpf_queued_op *q_op_p);

static inline void dbpf_queued_op_dequeue(struct dbpf_queued_op *q_op_p)
{
    gen_mutex_lock(&dbpf_op_queue_mutex);

	/* FOR TESTING ONLY */
    if (q_op_p->op.state == OP_DEQUEUED) {
	printf("tried to dequeue already dequeued op.\n");

	gen_mutex_unlock(&dbpf_op_queue_mutex);
	return;
    }
	
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
 */

#endif
