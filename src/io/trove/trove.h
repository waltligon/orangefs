/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_H
#define __TROVE_H

#include <trove-types.h>
#include <trove-proto.h>

enum { 
	TROVE_ITERATE_START= 1,
	TROVE_ITERATE_END  = 2
};

enum { TROVE_PLAIN_FILE,
	TROVE_DIR,
};
#endif
