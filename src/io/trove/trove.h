/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_H
#define __TROVE_H

#include <limits.h>
#include <errno.h>

#include "pvfs2-config.h"
#include "pvfs2-debug.h"
#include "pvfs2-req-proto.h"

#include "trove-types.h"
#include "trove-proto.h"

#define TROVE_MAX_CONTEXTS                 16
#define TROVE_DEFAULT_TEST_TIMEOUT         10
#define TROVE_DEFAULT_HANDLE_PURGATORY_SEC 45

enum
{
    TROVE_ITERATE_START = PVFS_ITERATE_START,
    TROVE_ITERATE_END   = PVFS_ITERATE_END 
};

enum
{
    TROVE_PLAIN_FILE,
    TROVE_DIR,
};

/* TROVE operation flags */
enum
{
    TROVE_SYNC   = 1,
    TROVE_ATOMIC = 2,
    TROVE_FORCE_REQUESTED_HANDLE = 4
};

/* get/setinfo option flags */
enum
{
    TROVE_COLLECTION_HANDLE_RANGES,
    TROVE_COLLECTION_HANDLE_TIMEOUT,
    TROVE_COLLECTION_ATTR_CACHE_KEYWORDS,
    TROVE_COLLECTION_ATTR_CACHE_SIZE,
    TROVE_COLLECTION_ATTR_CACHE_MAX_NUM_ELEMS,
    TROVE_COLLECTION_ATTR_CACHE_INITIALIZE
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
