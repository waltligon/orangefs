/*
 * (C) 2008 Clemson University and The University of Chicago
 * 
 * See COPYING in top-level directory.
 */

#ifndef _CAPCACHE_H_
#define _CAPCACHE_H_


#include "pvfs2-types.h"
#include "tcache.h"
#include "pint-security.h"


#define PINT_capcache_options PINT_tcache_options
enum
{
    CAPCACHE_TIMEOUT_MSECS = TCACHE_TIMEOUT_MSECS,
    CAPCACHE_NUM_ENTRIES = TCACHE_NUM_ENTRIES,
    CAPCACHE_HARD_LIMIT = TCACHE_HARD_LIMIT,
    CAPCACHE_SOFT_LIMIT = TCACHE_SOFT_LIMIT,
    CAPCACHE_ENABLE = TCACHE_ENABLE,
    CAPCACHE_RECLAIM_PERCENTAGE = TCACHE_RECLAIM_PERCENTAGE
};

int PINT_capcache_initialize(void);

void PINT_capcache_finalize(void);

int PINT_capcache_get_info(
    enum PINT_capcache_options option,
    unsigned int *arg);

int PINT_capcache_set_info(
    enum PINT_capcache_options option,
    unsigned int arg);

int PINT_capcache_lookup(
    PVFS_object_ref *objref,
    PVFS_capability **capability);

int PINT_capcache_insert(
    PVFS_object_ref *objref,
    PVFS_capability *capability);


#endif /* _CAPCACHE_H_ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
