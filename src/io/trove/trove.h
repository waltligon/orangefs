/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_H
#define __TROVE_H

#include "pvfs2-config.h"

#include "trove-types.h"
#include "trove-proto.h"

enum { 
    TROVE_ITERATE_START= 1,
    TROVE_ITERATE_END  = 2
};

enum {
    TROVE_PLAIN_FILE,
    TROVE_DIR,
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts = 4 sw=4 noexpandtab
 */

#endif
