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

#include <string.h>
#include <assert.h>

#include "quicklist.h"
#include "trove.h"
#include "dbpf.h"

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

/* struct dbpf_queued_op_stats
 *
 * used to maintain any desired statistics on the queued operation;
 * how many times we have worked on it, when it was queued, whatever.
 *
 * TODO: maybe merge these values into the main structure later?
 */
struct dbpf_queued_op_stats
{
    int svc_ct;
};

/* struct dbpf_queued_op
 *
 * used to maintain an in-memory account of operations that have been
 * queued
 *
 */
typedef struct
{
    gen_mutex_t mutex;
    struct dbpf_op op;
    struct dbpf_queued_op_stats stats;
    struct qlist_head link;
} dbpf_queued_op_t;

/***********************************
 * dbpf_queued_op_t specific operations
 ***********************************/
dbpf_queued_op_t *dbpf_queued_op_alloc(
    void);

void dbpf_queued_op_init(
    dbpf_queued_op_t *q_op_p,
    enum dbpf_op_type type,
    TROVE_handle handle,
    struct dbpf_collection *coll_p,
    int (*svc_fn)(struct dbpf_op *op),
    void *user_ptr,
    TROVE_ds_flags flags,
    TROVE_context_id context_id);

void dbpf_queued_op_free(
    dbpf_queued_op_t *q_op_p);

TROVE_op_id dbpf_queued_op_queue(
    dbpf_queued_op_t *q_op_p);

void dbpf_queued_op_put_and_dequeue(
    dbpf_queued_op_t *q_op_p);

void dbpf_queue_list(
    void);

void dbpf_queued_op_touch(
    dbpf_queued_op_t *q_op_p);



typedef struct qlist_head *dbpf_op_queue_p;

/***********************************
 * dbpf_op_queue specific operations
 ***********************************/
dbpf_op_queue_p dbpf_op_queue_new(
    void);

void dbpf_op_queue_cleanup(
    dbpf_op_queue_p op_queue);

void dbpf_op_queue_add(
    dbpf_op_queue_p op_queue,
    dbpf_queued_op_t *dbpf_op);

void dbpf_op_queue_remove(
    dbpf_queued_op_t *dbpf_op);

int dbpf_op_queue_empty(
    dbpf_op_queue_p op_queue);

dbpf_queued_op_t *dbpf_op_queue_shownext(
    dbpf_op_queue_p op_queue);

int dbpf_queued_op_try_get(
    TROVE_op_id id,
    dbpf_queued_op_t **q_op_pp);

void dbpf_queued_op_put(
    dbpf_queued_op_t *q_op_p,
    int completed);

enum
{
    DBPF_QUEUED_OP_INVALID = -1,
    DBPF_QUEUED_OP_BUSY = 0,
    DBPF_QUEUED_OP_SUCCESS = 1
};

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
