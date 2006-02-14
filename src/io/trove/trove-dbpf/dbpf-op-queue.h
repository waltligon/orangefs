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

typedef struct qlist_head *dbpf_op_queue_p;

dbpf_op_queue_p dbpf_op_queue_new(void);

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

TROVE_op_id dbpf_queued_op_queue(
    dbpf_queued_op_t *q_op_p);

TROVE_op_id dbpf_queued_op_queue_nolock(
    dbpf_queued_op_t *q_op_p);

int dbpf_queued_op_try_get(
    TROVE_op_id id,
    dbpf_queued_op_t **q_op_pp);

void dbpf_queued_op_put(
    dbpf_queued_op_t *q_op_p,
    int completed);

void dbpf_queued_op_dequeue(
    dbpf_queued_op_t *q_op_p);

void dbpf_queued_op_put_and_dequeue(
    dbpf_queued_op_t *q_op_p);

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
    TROVE_ds_flags flags,
    TROVE_op_id *out_op_id_p);

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
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
