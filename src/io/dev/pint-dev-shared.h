/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* types and #defines shared between user space and kernel space for
 * device interaction
 */

#ifndef __PINT_DEV_SHARED_H
#define __PINT_DEV_SHARED_H

/* supported ioctls */
#define PVFS_DEV_GET_MAGIC                      1
#define PVFS_DEV_GET_MAX_UPSIZE                 2
#define PVFS_DEV_GET_MAX_DOWNSIZE               3

#endif /* __PINT_DEV_SHARED_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
