/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

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

/* misc constants */
#define PVFS2_NAME_LEN                 0x000000FF
#define MAX_DIRENT_COUNT               0x00000020

#include "pvfs2-types.h"
#include "pvfs2-storage.h"
#include "pvfs2-attr.h"

#include "upcall.h"
#include "downcall.h"
#include "quickhash.h"

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
