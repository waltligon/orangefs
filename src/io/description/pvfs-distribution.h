/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS_DISTRIBUTION_H
#define __PVFS_DISTRIBUTION_H

#include "pvfs2-types.h"

extern PINT_dist *PVFS_dist_create(const char *name);
extern int PVFS_dist_free(PINT_dist *dist);
extern PINT_dist *PVFS_Dist_copy(const PINT_dist *dist);
extern int PVFS_dist_getparams(void *buf, const PINT_dist *dist);
extern int PVFS_dist_setparams(PINT_dist *dist, const void *buf);


#endif /* __PVFS_DISTRIBUTION_H */

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
