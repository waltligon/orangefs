/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <string.h>
#include <assert.h>

#include "dbpf-op.h"
#include "dbpf-bstream.h"
#include "gossip.h"

dbpf_queued_op_t *dbpf_queued_op_alloc(void)
{
    return (dbpf_queued_op_t *)malloc(sizeof(dbpf_queued_op_t));
}

/* dbpf_queued_op_init()
 *
 * Initializes a dbpf_queued_op_t structure.  Afterwards the op union and
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
    q_op_p->state         = 0;
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
        q_op_p->op.u.d_create.extent_array.extent_array = NULL;
    }
    else if ((q_op_p->op.type == BSTREAM_READ_LIST) ||
             (q_op_p->op.type == BSTREAM_WRITE_LIST))
    {
        if (q_op_p->op.u.b_rw_list.fd != -1)
        {
            dbpf_bstream_fdcache_put(q_op_p->op.coll_p->coll_id,
                                     q_op_p->op.handle);
            q_op_p->op.u.b_rw_list.fd = -1;
        }
        if (q_op_p->op.u.b_rw_list.aiocb_array)
        {
            free(q_op_p->op.u.b_rw_list.aiocb_array);
            q_op_p->op.u.b_rw_list.aiocb_array = NULL;
        }
    }
    free(q_op_p);
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
