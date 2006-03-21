/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_OPEN_CACHE_H__
#define __DBPF_OPEN_CACHE_H__

#include <db.h>

#include "trove.h"
#include "trove-internal.h"

struct open_cache_ref
{
    int fd;
    void* internal; /* pointer to underlying data structure */
};

enum open_ref_type
{
    DBPF_OPEN_BSTREAM   = 1,
};

void dbpf_open_cache_initialize(void);

void dbpf_open_cache_finalize(void);

int dbpf_open_cache_get(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    int create_flag,
    enum open_ref_type type,
    struct open_cache_ref* out_ref);

void dbpf_open_cache_put(
    struct open_cache_ref* in_ref);

int dbpf_open_cache_remove(
    TROVE_coll_id coll_id,
    TROVE_handle handle);

#endif /* __DBPF_OPEN_CACHE_H__ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
