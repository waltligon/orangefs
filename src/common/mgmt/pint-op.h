/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef PINT_OP_H
#define PINT_OP_H

#include "pint-queue.h"

typedef PVFS_id_gen_t PINT_op_id;

/* returns 0 on sucess or
 * a negative error value
 */
typedef int (* PINT_service_callout)(
    void *user_ptr, PVFS_hint hints);

typedef struct
{
    PINT_op_id id;
    PINT_service_callout operation;
    PINT_service_callout cancel;
    void *operation_ptr;
    PVFS_hint hint;

    /* Used to manage things like time in queue, time servicing, etc. */
    struct timeval timestamp;

    /* Used by the queue this op is added to.  Prevents extra mem allocation */
    PINT_queue_entry_t qentry;
} PINT_operation_t;

int PINT_op_queue_find_op_id_callback(PINT_queue_entry_t *entry, void *user_ptr);

#define PINT_operation_fill(op, id, fn, ptr, hint) \
    do {                                           \
        op->op_id = id;                            \
        op->fn = operation;                        \
        op->operation_ptr = ptr;                   \
        op->hint = hint;                           \
    } while(0)

#endif

#define PINT_op_from_qentry(qe) \
    PINT_queue_entry_object((qe), PINT_operation_t, qentry)


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
