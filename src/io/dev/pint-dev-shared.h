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
#define PVFS_DEV_MAP				4

/* This is the number of discrete buffers we will break the mapped I/O 
 * region into.  In some sense it governs the number of concurrent I/O
 * operations that we will allow
 */
#define PVFS2_BUFMAP_DESC_COUNT    5

/*
  by default, we assume each description size is 4MB;
  this value dictates the initial blocksize stored in
  the superblock, but after the request device is
  initialized, a subsequent statfs updates the superblock
  blocksize to be the configured decsription size (gathered
  using pvfs_bufmap_size_query).
*/
#define PVFS2_BUFMAP_DEFAULT_DESC_SIZE  (4 * (1024 * 1024))

/* size of mapped buffer region to use for I/O transfers (in bytes) */
#define PVFS2_BUFMAP_TOTAL_SIZE \
(PVFS2_BUFMAP_DESC_COUNT * PVFS2_BUFMAP_DEFAULT_DESC_SIZE)


/* describes memory regions to map in the PVFS_DEV_MAP ioctl */
struct PVFS_dev_map_desc
{
    void* ptr;
    int size;
};

#endif /* __PINT_DEV_SHARED_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
