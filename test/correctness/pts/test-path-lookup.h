/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TEST_PATH_LOOKUP_H
#define __TEST_PATH_LOOKUP_H

#include "pvfs2-types.h"

int test_path_lookup(MPI_Comm *comm, int rank, char *buf, void *params);

#endif /* __TEST_PATH_LOOKUP_H */

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
