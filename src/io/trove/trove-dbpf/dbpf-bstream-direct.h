/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_BSTREAM_DIRECT_H
#define __DBPF_BSTREAM_DIRECT_H

#include "trove-types.h"

int dbpf_bstream_direct_read_op_svc(void *ptr, TROVE_hint *hints);
int dbpf_bstream_direct_write_op_svc(void *ptr, TROVE_hint *hints);

#endif
