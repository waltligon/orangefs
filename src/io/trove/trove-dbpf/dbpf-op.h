/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_OP_H__
#define __DBPF_OP_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "quicklist.h"
#include "trove.h"
#include "dbpf.h"

#include "id-generator.h"

#define DBPF_OP_INIT(_op, _type, _state, _handle, _coll_p, _svc_fn, \
                     _user_ptr, _flags, _context_id, _id) \
    do { \
        (_op).type = _type; \
        (_op).state = OP_NOT_QUEUED; \
        (_op).handle = _handle; \
        (_op).coll_p = _coll_p; \
        (_op).svc_fn = _svc_fn; \
        (_op).user_ptr = _user_ptr; \
        (_op).flags = _flags; \
        (_op).context_id = _context_id; \
        id_gen_fast_register(&(_op).id, _id); \
    } while(0)
    
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

    /* the operation return code after being services */
    TROVE_ds_state state;

    struct qlist_head link;
} dbpf_queued_op_t;

dbpf_queued_op_t *dbpf_queued_op_alloc(void);

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

void dbpf_queued_op_touch(
    dbpf_queued_op_t *q_op_p);

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
