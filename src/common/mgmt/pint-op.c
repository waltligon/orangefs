/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pint-op.h"
#include <string.h>

int PVFS_hint_copy(PVFS_hint *src, PVFS_hint *dest)
{
    if(!src)
    {
        return -PVFS_EINVAL;
    }

    dest->type = strdup(src->type);
    dest->hint = malloc(src->length);
    if(!dest->hint)
    {
        return -PVFS_ENOMEM;
    }

    memcpy(dest->hint, src->hint, src->length);

    if(src->next)
    {
        dest->next = malloc(sizeof(PVFS_hint));
        PVFS_hint_copy(src->next, dest->next);
    }
    else
    {
        dest->next = NULL;
    }

    return 0;
}

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

