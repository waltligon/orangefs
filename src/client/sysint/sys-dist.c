/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include "pvfs2-sysint.h"

/**
 * Return the distribution associated with the given identifier, or null
 * if none exists
 */
PVFS_sys_dist* PVFS_sys_dist_lookup(const char* dist_identifier)
{
    return 0;
}

/**
 * Free resources associated with this distribution
 */
int PVFS_sys_dist_free(PVFS_sys_dist* dist)
{
    free(dist);
    return 0;
}

/**
 * Set the named distribution parameter with the given value
 */
int PVFS_sys_dist_setparam(
    PVFS_sys_dist* dist,
    const char* param,
    void* value)
{
    return 0;
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 noexpandtab
 */
