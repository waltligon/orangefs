/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file includes definitions of common internal utility functions */

#include "pvfs2-types.h"
#include "pint-util.h"
#include "gen-locks.h"

static int current_tag;
static gen_mutex_t current_tag_lock = GEN_MUTEX_INITIALIZER;

PVFS_msg_tag_t PINT_util_get_next_tag(void)
{
    PVFS_msg_tag_t ret;

    gen_mutex_lock(&current_tag_lock);
    ret = current_tag;

    /* increment the tag, don't use zero */
    if (current_tag + 1 == 0)
    {
	current_tag = 1;
    }
    else
    {
	current_tag++;
    }
    gen_mutex_unlock(&current_tag_lock);

    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
