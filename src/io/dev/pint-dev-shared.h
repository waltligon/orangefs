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

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>  /* needed for constructing the _IO macros */
#endif

/* version number for use in communicating between kernel space and user
 * space
 */
#define PVFS_KERNEL_PROTO_VERSION ((PVFS2_VERSION_MAJOR * 10000) + \
  (PVFS2_VERSION_MINOR * 100) + PVFS2_VERSION_SUB)

/* This is the number of discrete buffers we will break the mapped I/O
 * region into.  In some sense it governs the number of concurrent I/O
 * operations that we will allow
 */
#define PVFS2_BUFMAP_DESC_COUNT    5

/*
  by default, we assume each description size is 4MB; this value
  dictates the initial blocksize stored in the superblock, but after
  the request device is initialized, a subsequent statfs updates the
  superblock blocksize to be the configured decsription size (gathered
  using pvfs_bufmap_size_query).

  don't change this value without updating the shift value below, or
  else we may break size reporting in the kernel
*/
#define PVFS2_BUFMAP_DEFAULT_DESC_SIZE  (4 * (1024 * 1024))
#define PVFS2_BUFMAP_DEFAULT_DESC_SHIFT 22 /* NOTE: 2^22 == 4MB */

/* size of mapped buffer region to use for I/O transfers (in bytes) */
#define PVFS2_BUFMAP_TOTAL_SIZE \
(PVFS2_BUFMAP_DESC_COUNT * PVFS2_BUFMAP_DEFAULT_DESC_SIZE)

/* pvfs2-client-core can cache readahead data up to this size in bytes */
#define PVFS2_MMAP_RACACHE_MAX_SIZE ((loff_t)(8 * (1024 * 1024)))

/* describes memory regions to map in the PVFS_DEV_MAP ioctl.
 * NOTE: See devpvfs2-req.c for 32 bit compat structure.
 * Since this structure has a variable-sized layout that is different
 * on 32 and 64 bit platforms, we need to normalize to a 64 bit layout
 * on such systems before servicing ioctl calls from user-space binaries
 * that may be 32 bit!
 */
struct PVFS_dev_map_desc
{
    void     *ptr;
    int32_t  size; /* Changed to an int32_t for fixed size structure */
};

#define PVFS_DEV_MAGIC 'k'

#define DEV_GET_MAGIC           0x1
#define DEV_GET_MAX_UPSIZE      0x2
#define DEV_GET_MAX_DOWNSIZE    0x3
#define DEV_MAP                 0x4
#define DEV_REMOUNT_ALL         0x5
#define DEV_MAX_NR              0x6

/* supported ioctls, codes are with respect to user-space */
enum {
PVFS_DEV_GET_MAGIC          = _IOW(PVFS_DEV_MAGIC, DEV_GET_MAGIC, int32_t),
PVFS_DEV_GET_MAX_UPSIZE     = _IOW(PVFS_DEV_MAGIC, DEV_GET_MAX_UPSIZE, int32_t),
PVFS_DEV_GET_MAX_DOWNSIZE   = _IOW(PVFS_DEV_MAGIC, DEV_GET_MAX_DOWNSIZE, int32_t),
PVFS_DEV_MAP                =  _IO(PVFS_DEV_MAGIC, DEV_MAP),
PVFS_DEV_REMOUNT_ALL        =  _IO(PVFS_DEV_MAGIC, DEV_REMOUNT_ALL),
PVFS_DEV_MAXNR              =  DEV_MAX_NR,
};

#endif /* __PINT_DEV_SHARED_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
