/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_H
#define __TROVE_H

#include <limits.h>

#include "pvfs2-config.h"

#include "trove-types.h"
#include "trove-proto.h"

enum { 
    TROVE_ITERATE_START = (INT_MAX - 1),
    TROVE_ITERATE_END   = (INT_MAX - 2)
};

enum {
    TROVE_PLAIN_FILE,
    TROVE_DIR,
};

/* TROVE operation flags */
enum {
    TROVE_SYNC   = 1,
    TROVE_ATOMIC = 2,
    TROVE_FORCE_REQUESTED_HANDLE = 3
};

/* get/setinfo option flags */
enum {
    TROVE_COLLECTION_HANDLE_RANGES
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
