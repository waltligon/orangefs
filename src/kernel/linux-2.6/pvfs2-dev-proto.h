
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

/* Misc constants. Please retain them as multiples of 8!
 * Otherwise 32-64 bit interactions will be messed up :)
 */
#define PVFS2_NAME_LEN                 0x00000100
/* MAX_DIRENT_COUNT cannot be larger than PVFS_REQ_LIMIT_LISTATTR */
#define MAX_DIRENT_COUNT               0x00000020

#include "pvfs2.h"

#include "upcall.h"
#include "downcall.h"
#include "quickhash.h"

/* The only reason these functions are in this header file is because we would like a 
 * common header file for both the encoding and decoding of the readdir/readdirplus
 * downcall response buffer and they need to be shared across user/kernel boundaries.
 * Also, these encoder/decoder macros don't do any byte-swapping.
 */

#ifndef roundup4
#define roundup4(x) (((x)+3) & ~3)
#endif

#ifndef roundup8
#define roundup8(x) (((x)+7) & ~7)
#endif

#define enc_int64_t(pptr,x) do { \
    *(int64_t*) *(pptr) = (*(x)); \
    *(pptr) += 8; \
} while (0)
#define dec_int64_t(pptr,x) do { \
    *(x) = (*(int64_t*) *(pptr)); \
    *(pptr) += 8; \
} while (0)

#define enc_int32_t(pptr,x) do { \
    *(int32_t*) *(pptr) = (*(x)); \
    *(pptr) += 4; \
} while (0)
#define dec_int32_t(pptr,x) do { \
    *(x) = (*(int32_t*) *(pptr)); \
    *(pptr) += 4; \
} while (0)

/* skip 4 bytes */
#define enc_skip4(pptr,x) do { \
    *(pptr) += 4; \
} while (0)
#define dec_skip4(pptr,x) do { \
    *(pptr) += 4; \
} while (0)

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

#define dec_here_string(pptr,pbuf) do { \
    u_int32_t len = (*(u_int32_t *) *(pptr)); \
    memcpy(pbuf, *(pptr) + 4, len + 1); \
    *(pptr) += roundup8(4 + len + 1); \
} while (0)

#ifndef __KERNEL__

/* encoding needed by client-core to copy readdir entries to the shared page */
#define encode_dirents(pptr, readdir) do {\
    int i; \
    enc_int32_t(pptr, &readdir->token);\
    enc_skip4(pptr,);\
    enc_int64_t(pptr, &readdir->directory_version);\
    enc_skip4(pptr,);\
    enc_int32_t(pptr, &readdir->pvfs_dirent_outcount);\
    for (i = 0; i < readdir->pvfs_dirent_outcount; i++) \
    { \
        enc_string(pptr, &readdir->dirent_array[i].d_name);\
        enc_int64_t(pptr, &readdir->dirent_array[i].handle);\
    } \
} while (0)

#define encode_sys_attr(pptr, readdirplus) do {\
    int i; \
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++) \
    { \
        enc_int32_t(pptr, &readdirplus->stat_err_array[i]);\
    } \
    if (readdirplus->pvfs_dirent_outcount % 2) { \
        enc_skip4(pptr,);\
    } \
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++) \
    { \
        enc_int32_t(pptr, &readdirplus->attr_array[i].owner);\
        enc_int32_t(pptr, &readdirplus->attr_array[i].group);\
        enc_int32_t(pptr, &readdirplus->attr_array[i].perms);\
        enc_skip4(pptr,); \
        enc_int64_t(pptr, &readdirplus->attr_array[i].atime);\
        enc_int64_t(pptr, &readdirplus->attr_array[i].mtime);\
        enc_int64_t(pptr, &readdirplus->attr_array[i].ctime);\
        enc_int32_t(pptr, &readdirplus->attr_array[i].objtype);\
        enc_int32_t(pptr, &readdirplus->attr_array[i].mask);\
        enc_int64_t(pptr, &readdirplus->attr_array[i].size);\
        enc_int64_t(pptr, &readdirplus->attr_array[i].dirent_count);\
        enc_int32_t(pptr, &readdirplus->attr_array[i].dfile_count); \
        enc_skip4(pptr,); \
        if (readdirplus->attr_array[i].link_target) \
        { \
            enc_string(pptr, &readdirplus->attr_array[i].link_target);\
        }\
    } \
} while (0)

/* DEBUG macro: used in client-core only */
#define decode_dirents(pptr, readdir) do { \
    int i; \
    dec_int32_t(pptr, &readdir->token);\
    dec_skip4(pptr,);\
    dec_int64_t(pptr, &readdir->directory_version);\
    dec_skip4(pptr,);\
    dec_int32_t(pptr, &readdir->pvfs_dirent_outcount);\
    readdir->dirent_array = (PVFS_dirent *) malloc(readdir->pvfs_dirent_outcount * sizeof(PVFS_dirent)); \
    if (readdir->dirent_array == NULL) \
    {\
        return -ENOMEM;\
    } \
    for (i = 0; i < readdir->pvfs_dirent_outcount; i++) \
    { \
        dec_here_string(pptr, readdir->dirent_array[i].d_name); \
        dec_int64_t(pptr, &readdir->dirent_array[i].handle); \
    } \
} while (0)

/* DEBUG macro: used in client-core only for testing */
#define decode_sys_attr(pptr, readdirplus) do { \
    int i; \
    readdirplus->stat_err_array = (int32_t *) malloc(readdirplus->pvfs_dirent_outcount * sizeof(int32_t)); \
    if (readdirplus->stat_err_array == NULL) \
    { \
        return -ENOMEM;\
    } \
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++) \
    { \
        dec_int32_t(pptr, &readdirplus->stat_err_array[i]);\
    } \
    if (readdirplus->pvfs_dirent_outcount % 2) { \
        dec_skip4(pptr,);\
    } \
    readdirplus->attr_array = (PVFS_sys_attr *) malloc(readdirplus->pvfs_dirent_outcount * sizeof(PVFS_sys_attr)); \
    if (readdirplus->attr_array == NULL) \
    { \
        return -ENOMEM;\
    } \
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++) \
    { \
        dec_int32_t(pptr, &readdirplus->attr_array[i].owner);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].group);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].perms);\
        dec_skip4(pptr,); \
        dec_int64_t(pptr, &readdirplus->attr_array[i].atime);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].mtime);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].ctime);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].objtype);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].mask);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].size);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].dirent_count);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].dfile_count); \
        dec_skip4(pptr,); \
        if (readdirplus->attr_array[i].objtype == PVFS_TYPE_SYMLINK && (readdirplus->attr_array[i].mask & PVFS_ATTR_SYS_LNK_TARGET)) \
        { \
            int len;\
            dec_string(pptr, &readdirplus->attr_array[i].link_target, &len);\
        } \
        else { \
            readdirplus->attr_array[i].link_target = NULL; \
        } \
    }\
} while (0)

#else

/* decode routine needed by kmod to make sense of the shared page for readdirs */
#define decode_dirents(pptr, readdir) do { \
    int i; \
    dec_int32_t(pptr, &readdir->token);\
    dec_skip4(pptr,);\
    dec_int64_t(pptr, &readdir->directory_version);\
    dec_skip4(pptr,);\
    dec_int32_t(pptr, &readdir->pvfs_dirent_outcount);\
    readdir->dirent_array = (struct pvfs2_dirent *) kmalloc(readdir->pvfs_dirent_outcount * sizeof(struct pvfs2_dirent), GFP_KERNEL); \
    if (readdir->dirent_array == NULL) \
    {\
        return -ENOMEM;\
    } \
    for (i = 0; i < readdir->pvfs_dirent_outcount; i++) \
    { \
        dec_string(pptr, &readdir->dirent_array[i].d_name, &readdir->dirent_array[i].d_length); \
        dec_int64_t(pptr, &readdir->dirent_array[i].handle); \
    } \
} while (0)


#define decode_sys_attr(pptr, readdirplus) do { \
    int i; \
    readdirplus->stat_err_array = (int32_t *) kmalloc(readdirplus->pvfs_dirent_outcount * sizeof(int32_t), GFP_KERNEL); \
    if (readdirplus->stat_err_array == NULL) \
    { \
        return -ENOMEM;\
    } \
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++) \
    { \
        dec_int32_t(pptr, &readdirplus->stat_err_array[i]);\
    } \
    if (readdirplus->pvfs_dirent_outcount % 2) { \
        dec_skip4(pptr,);\
    } \
    readdirplus->attr_array = (PVFS_sys_attr *) kmalloc(readdirplus->pvfs_dirent_outcount * sizeof(PVFS_sys_attr), GFP_KERNEL); \
    if (readdirplus->attr_array == NULL) \
    { \
        return -ENOMEM;\
    } \
    for (i = 0; i < readdirplus->pvfs_dirent_outcount; i++) \
    { \
        dec_int32_t(pptr, &readdirplus->attr_array[i].owner);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].group);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].perms);\
        dec_skip4(pptr,); \
        dec_int64_t(pptr, &readdirplus->attr_array[i].atime);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].mtime);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].ctime);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].objtype);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].mask);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].size);\
        dec_int64_t(pptr, &readdirplus->attr_array[i].dirent_count);\
        dec_int32_t(pptr, &readdirplus->attr_array[i].dfile_count); \
        dec_skip4(pptr,); \
        if (readdirplus->attr_array[i].objtype == PVFS_TYPE_SYMLINK && (readdirplus->attr_array[i].mask & PVFS_ATTR_SYS_LNK_TARGET)) \
        { \
            int len;\
            dec_string(pptr, &readdirplus->attr_array[i].link_target, &len);\
        } \
        else { \
            readdirplus->attr_array[i].link_target = NULL; \
        } \
    }\
} while (0)

#endif

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
