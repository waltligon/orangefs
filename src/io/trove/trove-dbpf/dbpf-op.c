/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "dbpf-op.h"
#include "gossip.h"



enum operation_queue_type dbpf_op_queue_get_queue_type(
    enum dbpf_op_type type)
{
    if (DBPF_OP_IS_BSTREAM(type))
        return OP_QUEUE_IO;
    if (DBPF_OP_MODIFYING_META_OP(type))
        return OP_QUEUE_META_WRITE;
    return OP_QUEUE_META_READ;
}

dbpf_queued_op_t *dbpf_queued_op_alloc(
    void)
{
    return (dbpf_queued_op_t *) malloc(sizeof(dbpf_queued_op_t));
}

/* dbpf_queued_op_init()
 *
 * Initializes a dbpf_queued_op_t structure.  Afterwards the op union and
 * the next_p must still be handled.
 */
void dbpf_queued_op_init(
    dbpf_queued_op_t * q_op_p,
    enum dbpf_op_type type,
    TROVE_handle handle,
    struct dbpf_collection *coll_p,
    int (*svc_fn) (struct dbpf_op * op),
    void *user_ptr,
    TROVE_ds_flags flags,
    TROVE_context_id context_id)
{
    assert(q_op_p);
    bzero(q_op_p, sizeof(dbpf_queued_op_t));

    INIT_QLIST_HEAD(&q_op_p->link);
    q_op_p->state = 0;
    q_op_p->queue_type = dbpf_op_queue_get_queue_type(type);
    q_op_p->op.type = type;
    q_op_p->op.state = OP_NOT_QUEUED;
    q_op_p->op.handle = handle;
    q_op_p->op.coll_p = coll_p;
    q_op_p->op.svc_fn = svc_fn;
    q_op_p->op.user_ptr = user_ptr;
    q_op_p->op.flags = flags;
    q_op_p->op.context_id = context_id;
    gen_posix_mutex_init(&q_op_p->op.state_mutex);

    id_gen_safe_register(&q_op_p->op.id, q_op_p);
}

void dbpf_queued_op_free(
    dbpf_queued_op_t * q_op_p)
{
    if (q_op_p->op.type == DSPACE_CREATE)
    {
        free(q_op_p->op.u.d_create.extent_array.extent_array);
        q_op_p->op.u.d_create.extent_array.extent_array = NULL;
    }
    else if ((q_op_p->op.type == BSTREAM_READ_LIST) ||
             (q_op_p->op.type == BSTREAM_WRITE_LIST))
    {

    }

    id_gen_safe_unregister(q_op_p->op.id);
    free(q_op_p);
}

/* dbpf_queued_op_touch()
 *
 * Notes in statistics that we have been working on this operation
 * again.
 */
void dbpf_queued_op_touch(
    dbpf_queued_op_t * q_op_p)
{
    q_op_p->stats.svc_ct++;
}

void dbpf_op_change_status(
    dbpf_queued_op_t * q_op_p,
    enum dbpf_op_state state)
{
    gen_mutex_lock(&q_op_p->op.state_mutex);
    q_op_p->op.state = state;
    gen_mutex_unlock(&q_op_p->op.state_mutex);
}

enum dbpf_op_state dbpf_op_get_status(
    dbpf_queued_op_t * q_op_p)
{
    enum dbpf_op_state state;
    gen_mutex_lock(&q_op_p->op.state_mutex);
    state = q_op_p->op.state;
    gen_mutex_unlock(&q_op_p->op.state_mutex);
    return state;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
