/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This header includes prototypes for common internal utility functions */

#ifndef __PINT_UTIL_H
#define __PINT_UTIL_H

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-attr.h"

/* converts common fields between sys attr and obj attr structures */
#define PINT_CONVERT_ATTR(dest, src, extra_amask)       \
do{                                                     \
    (dest)->mask = 0;                                   \
    if ((src)->mask & PVFS_ATTR_SYS_UID)                \
    {                                                   \
        (dest)->owner = (src)->owner;                   \
        (dest)->mask  |= PVFS_ATTR_COMMON_UID;          \
    }                                                   \
    if ((src)->mask & PVFS_ATTR_SYS_GID)                \
    {                                                   \
        (dest)->group = (src)->group;                   \
        (dest)->mask |= PVFS_ATTR_COMMON_GID;           \
    }                                                   \
    if ((src)->mask & PVFS_ATTR_SYS_PERM)               \
    {                                                   \
        (dest)->perms = (src)->perms;                   \
        (dest)->mask |= PVFS_ATTR_COMMON_PERM;          \
    }                                                   \
    if ((src)->mask & PVFS_ATTR_SYS_ATIME)              \
    {                                                   \
        (dest)->mask |= PVFS_ATTR_COMMON_ATIME;         \
        if ((src)->mask & PVFS_ATTR_SYS_ATIME_SET)      \
        {                                               \
            (dest)->atime = (src)->atime;               \
            (dest)->mask |= PVFS_ATTR_COMMON_ATIME_SET; \
        }                                               \
    }                                                   \
    if ((src)->mask & PVFS_ATTR_SYS_MTIME)              \
    {                                                   \
        (dest)->mask |= PVFS_ATTR_COMMON_MTIME;         \
        if ((src)->mask & PVFS_ATTR_SYS_MTIME_SET)      \
        {                                               \
            (dest)->mtime = (src)->mtime;               \
            (dest)->mask |= PVFS_ATTR_COMMON_MTIME_SET; \
        }                                               \
    }                                                   \
    if ((src)->mask & PVFS_ATTR_SYS_CTIME)              \
    {                                                   \
        (dest)->mask |= PVFS_ATTR_COMMON_CTIME;         \
    }                                                   \
    if ((src)->mask & PVFS_ATTR_SYS_TYPE)               \
    {                                                   \
        (dest)->objtype = (src)->objtype;               \
        (dest)->mask |= PVFS_ATTR_COMMON_TYPE;          \
    }                                                   \
    (dest)->mask |= (extra_amask);                      \
}while(0)

struct PINT_time_marker_s
{
    struct timeval wtime; /* real time */
    struct timeval utime; /* user time */
    struct timeval stime; /* system time */
};
typedef struct PINT_time_marker_s PINT_time_marker;

/* reserved tag values */
#define PINT_MSG_TAG_INVALID 0

PVFS_msg_tag_t PINT_util_get_next_tag(void);

int PINT_copy_object_attr(PVFS_object_attr *dest, PVFS_object_attr *src);
void PINT_free_object_attr(PVFS_object_attr *attr);
void PINT_time_mark(PINT_time_marker* out_marker);
void PINT_time_diff(PINT_time_marker mark1, 
    PINT_time_marker mark2,
    double* out_wtime_sec,
    double* out_utime_sec,
    double* out_stime_sec);

#ifdef HAVE_SYS_VFS_H

#include <sys/vfs.h>
#define PINT_statfs_t struct statfs
#define PINT_statfs_lookup(_path, _statfs) statfs(_path, (_statfs))
#define PINT_statfs_fd_lookup(_fd, _statfs) fstatfs(_fd, (_statfs))
#define PINT_statfs_bsize(_statfs) (_statfs)->f_bsize
#define PINT_statfs_bavail(_statfs) (_statfs)->f_bavail
#define PINT_statfs_bfree(_statfs) (_statfs)->f_bfree
#define PINT_statfs_blocks(_statfs) (_statfs)->f_blocks
#define PINT_statfs_fsid(_statfs) (_statfs)->f_fsid

#elif HAVE_SYS_MOUNT_H

#include <sys/param.h>
#include <sys/mount.h>

#define PINT_statfs_t struct statfs
#define PINT_statfs_lookup(_path, _statfs) statfs(_path, (_statfs))
#define PINT_statfs_fd_lookup(_fd, _statfs) fstatfs(_fd, (_statfs))
#define PINT_statfs_bsize(_statfs) (_statfs)->f_iosize
#define PINT_statfs_bavail(_statfs) (_statfs)->f_bavail
#define PINT_statfs_bfree(_statfs) (_statfs)->f_bfree
#define PINT_statfs_blocks(_statfs) (_statfs)->f_blocks
#define PINT_statfs_fsid(_statfs) (_statfs)->f_fsid

#elif defined(WIN32)
/* the statfs type must be defined on Windows */
typedef unsigned long fsid_t;
struct statfs {
    uint64_t f_type;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    fsid_t   f_fsid;
    uint64_t f_namelen;
};

#define PINT_statfs_t struct statfs
/* Use functions with Windows API calls--defined in pint-util.c */
int PINT_statfs_lookup(const char *path, struct statfs *buf);
int PINT_statfs_fd_lookup(int fd, struct statfs *buf);
#define PINT_statfs_bsize(_statfs) (_statfs)->f_bsize
#define PINT_statfs_bavail(_statfs) (_statfs)->f_bavail
#define PINT_statfs_bfree(_statfs) (_statfs)->f_bfree
#define PINT_statfs_blocks(_statfs) (_statfs)->f_blocks
#define PINT_statfs_fsid(_statfs) (_statfs)->f_fsid

#else

#error OS does not have sys/vfs.h or sys/mount.h.  
#error Cant stat mounted filesystems

#endif

char *PINT_util_get_object_type(int objtype);
PVFS_time PINT_util_get_current_time(void);
PVFS_time PINT_util_get_time_ms(void);
PVFS_time PINT_util_get_time_us(void);
void PINT_util_get_current_timeval(struct timeval *tv);
int PINT_util_get_timeval_diff(struct timeval *tv_start, struct timeval *tv_end);
void PINT_util_parse_timeval(struct timeval tv, char *str);

PVFS_time PINT_util_mktime_version(PVFS_time time);
PVFS_time PINT_util_mkversion_time(PVFS_time version);

struct timespec PINT_util_get_abs_timespec(int microsecs);

PVFS_uid PINT_util_getuid(void);
PVFS_gid PINT_util_getgid(void);

char *PINT_util_bytes2str(unsigned char *bytes, char *output, size_t count);

void PINT_util_digest_init(void);
void PINT_util_digest_finalize(void);

int PINT_util_digest_sha1(const void *input_message, size_t input_length,
		char **output, size_t *output_length);

int PINT_util_digest_md5(const void *input_message, size_t input_length,
		char **output, size_t *output_length);

char *PINT_util_guess_alias(void);

#endif /* __PINT_UTIL_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
