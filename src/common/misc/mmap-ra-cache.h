/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __MMAP_RA_CACHE_H
#define __MMAP_RA_CACHE_H

#include "quickhash.h"

typedef struct
{
    struct qlist_head hash_link;

    PVFS_object_ref refn;
    void *data;
    PVFS_size data_sz;

} mmap_ra_cache_elem_t;


/***********************************************
 * mmap_ra_cache methods - specifically for
 * caching data temporarily to optimize for
 * vfs mmap/execution cases, where many calls
 * are made for small amounts of data.
 *
 * all methods return 0 on success; -1 on failure
 * (unless noted)
 *
 ***********************************************/

int pvfs2_mmap_ra_cache_initialize(void);

int pvfs2_mmap_ra_cache_register(PVFS_object_ref refn,
                                 void *data, int data_len);

/*
  returns 0 on cache hit
  returns -1 on cache miss
  returns -2 if the data matching the refn should be flushed
*/
int pvfs2_mmap_ra_cache_get_block(
    PVFS_object_ref refn, PVFS_size offset,
    PVFS_size len, void *dest, int *amt_returned);

int pvfs2_mmap_ra_cache_flush(PVFS_object_ref refn);

int pvfs2_mmap_ra_cache_finalize(void);

#endif /* __MMAP_RA_CACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
