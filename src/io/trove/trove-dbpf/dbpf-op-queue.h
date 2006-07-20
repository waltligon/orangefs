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
#include "dbpf-op.h"

#include "id-generator.h"

typedef struct dbpf_op_queue_t{
    struct qlist_head list;
    int elems;
} dbpf_op_queue_s;

enum
{
    DBPF_QUEUED_OP_INVALID = -1,
    DBPF_QUEUED_OP_BUSY = 0,
    DBPF_QUEUED_OP_SUCCESS = 1
};

extern pthread_cond_t dbpf_op_incoming_cond[OP_QUEUE_LAST];

/* the queue that stores pending serviceable operations */
extern dbpf_op_queue_s dbpf_op_queue[OP_QUEUE_LAST];

/* lock to be obtained before manipulating dbpf_op_queue */
extern gen_mutex_t dbpf_op_queue_mutex[OP_QUEUE_LAST];

extern dbpf_op_queue_s * dbpf_completion_queue_array[TROVE_MAX_CONTEXTS];
extern gen_mutex_t    *dbpf_completion_queue_array_mutex[TROVE_MAX_CONTEXTS];
extern pthread_cond_t  dbpf_op_completed_cond;

extern int dbpf_op_queue_waiting_operation_count[OP_QUEUE_LAST];

extern pthread_cond_t dbpf_op_completed_cond;



dbpf_op_queue_s * dbpf_op_queue_new(void);
void dbpf_op_queue_init(dbpf_op_queue_s * queue);

void dbpf_move_op_to_completion_queue(dbpf_queued_op_t *cur_op, 
    TROVE_ds_state ret_state, enum dbpf_op_state end_state);
void dbpf_move_op_to_completion_queue_nolock(dbpf_queued_op_t *cur_op, 
    TROVE_ds_state ret_state, enum dbpf_op_state end_state);    

void dbpf_op_queue_cleanup_nolock(dbpf_op_queue_s *  op_queue);

void dbpf_op_queue_add( dbpf_op_queue_s *  op_queue,
    dbpf_queued_op_t *dbpf_op);

void dbpf_op_queue_remove(dbpf_queued_op_t *dbpf_op);

int dbpf_op_queue_empty(dbpf_op_queue_s *  op_queue);

dbpf_queued_op_t *dbpf_op_pop_front_nolock(dbpf_op_queue_s *  op_queue);

TROVE_op_id dbpf_queued_op_queue(dbpf_queued_op_t *q_op_p, 
    dbpf_op_queue_s* queue );
TROVE_op_id dbpf_queued_op_queue_nolock(dbpf_queued_op_t *q_op_p, 
    dbpf_op_queue_s* queue);

void dbpf_queued_op_dequeue(dbpf_queued_op_t *q_op_p );
void dbpf_queued_op_dequeue_nolock(dbpf_queued_op_t *q_op_p );

int dbpf_op_init_queued_or_immediate(
    struct dbpf_op *op_p,
    dbpf_queued_op_t **q_op_pp,
    enum dbpf_op_type op_type,
    struct dbpf_collection *coll_p,
    TROVE_handle handle,
    int (* dbpf_op_svc_fn)(struct dbpf_op *),
    TROVE_ds_flags flags,
    TROVE_vtag_s *vtag,
    void *user_ptr,
    TROVE_context_id context_id,
    struct dbpf_op **op_pp);

int dbpf_queue_or_service(
    struct dbpf_op *op_p,
    dbpf_queued_op_t *q_op_p,
    struct dbpf_collection *coll_p,
    TROVE_op_id *out_op_id_p);

int dbpf_queued_op_complete(dbpf_queued_op_t * op,
                            TROVE_ds_state ret,
                            enum dbpf_op_state state);
                            
int dbpf_queued_op_sync_coalesce_db_ops(
    dbpf_queued_op_t *qop_p);

int dbpf_queued_op_sync_coalesce_dequeue(
    dbpf_queued_op_t *qop_p);

int dbpf_queued_op_sync_coalesce_enqueue(
    dbpf_queued_op_t *qop_p);


#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
