
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef _PVFS2_DEV_PROTO_H
#define _PVFS2_DEV_PROTO_H

/* types and constants shared between user space and kernel space for
 * device interaction using a common protocol
 */

/************************************
 * valid pvfs2 kernel operation types
 ************************************/
#define PVFS2_VFS_OP_INVALID           0xFF000000
#define PVFS2_VFS_OP_FILE_IO           0xFF000001
#define PVFS2_VFS_OP_LOOKUP            0xFF000002
#define PVFS2_VFS_OP_CREATE            0xFF000003
#define PVFS2_VFS_OP_GETATTR           0xFF000004
#define PVFS2_VFS_OP_REMOVE            0xFF000005
#define PVFS2_VFS_OP_MKDIR             0xFF000006
#define PVFS2_VFS_OP_READDIR           0xFF000007
#define PVFS2_VFS_OP_SETATTR           0xFF000008
#define PVFS2_VFS_OP_SYMLINK           0xFF000009
#define PVFS2_VFS_OP_RENAME            0xFF00000A
#define PVFS2_VFS_OP_STATFS            0xFF00000B
#define PVFS2_VFS_OP_TRUNCATE          0xFF00000C
#define PVFS2_VFS_OP_MMAP_RA_FLUSH     0xFF00000D
#define PVFS2_VFS_OP_FS_MOUNT          0xFF00000E
#define PVFS2_VFS_OP_FS_UMOUNT         0xFF00000F
#define PVFS2_VFS_OP_GETXATTR          0xFF000010
#define PVFS2_VFS_OP_SETXATTR          0xFF000011
#define PVFS2_VFS_OP_LISTXATTR         0xFF000012
#define PVFS2_VFS_OP_REMOVEXATTR       0xFF000013
#define PVFS2_VFS_OP_PARAM             0xFF000014
#define PVFS2_VFS_OP_PERF_COUNT        0xFF000015
#define PVFS2_VFS_OP_CANCEL            0xFF00EE00
#define PVFS2_VFS_OP_FSYNC             0xFF00EE01
#define PVFS2_VFS_OP_FSKEY             0xFF00EE02
#define PVFS2_VFS_OP_READDIRPLUS       0xFF00EE03
#define PVFS2_VFS_OP_FILE_IOX          0xFF00EE04

/* Misc constants. Please retain them as multiples of 8!
 * Otherwise 32-64 bit interactions will be messed up :)
 */
#define PVFS2_NAME_LEN                 0x00000100
/* MAX_DIRENT_COUNT cannot be larger than PVFS_REQ_LIMIT_LISTATTR */
#define MAX_DIRENT_COUNT               0x00000060

#include "pvfs2.h"

#include "upcall.h"
#include "downcall.h"
#include "quickhash.h"

/*
 * These macros differ from proto macros in that they don't do any
 * byte-swappings and are used to ensure that kernel-clientcore interactions
 * don't cause any unaligned accesses etc on 64 bit machines
 */
#ifndef roundup4
#define roundup4(x) (((x)+3) & ~3)
#endif

#ifndef roundup8
#define roundup8(x) (((x)+7) & ~7)
#endif

/* strings; decoding just points into existing character data */
#define enc_string(pptr,pbuf) do { \
    u_int32_t len = strlen(*pbuf); \
    *(u_int32_t *) *(pptr) = (len); \
    memcpy(*(pptr)+4, *pbuf, len+1); \
    *(pptr) += roundup8(4 + len + 1); \
} while (0)

#define dec_string(pptr,pbuf, plen) do { \
    u_int32_t len = (*(u_int32_t *) *(pptr)); \
    *pbuf = *(pptr) + 4; \
    *(pptr) += roundup8(4 + len + 1); \
    if (plen) \
    *plen = len;\
} while (0)

struct read_write_x {
    PVFS_offset off;
    PVFS_size   len;
};

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
