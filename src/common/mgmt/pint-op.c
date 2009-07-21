/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pint-op.h"
#include <string.h>

int PINT_op_queue_find_op_id_callback(PINT_queue_entry_t *entry, void *user_ptr)
{
    if(*((PINT_op_id *)user_ptr) == PINT_op_from_qentry(entry)->id)
    {
        return 1;
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

