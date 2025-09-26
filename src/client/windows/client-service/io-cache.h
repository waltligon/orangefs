/*
 * (C) 2010-2022 and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

/*
 * IO cache declarations
 */

#ifndef __IO_CACHE_H
#define __IO_CACHE_H

#include <Windows.h>

#include "pvfs2.h"
#include "quickhash.h"

#define IO_CACHE_HIT    0
#define IO_CACHE_MISS   1

#define IO_CACHE_NO_UPDATE    0
#define IO_CACHE_UPDATE       1

struct io_cache_entry
{
    struct qhash_head hash_link;
    ULONG64 context;
    PVFS_object_ref object_ref;
    enum PVFS_io_type io_type;
    int update_flag;
};

int io_cache_compare(const void *key,
                     struct qhash_head *link);

int io_cache_add(ULONG64 context, 
                 PVFS_object_ref *object_ref,
                 enum PVFS_io_type io_type,
                 int update_flag);

int io_cache_remove(ULONG64 context);

int io_cache_get(ULONG64 context, 
                 PVFS_object_ref *object_ref, 
                 enum PVFS_io_type *io_type,
                 int *update_flag);

#endif