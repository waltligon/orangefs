/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* PVFS Types
 */
#ifndef __PVFS2_TYPES_H
#define __PVFS2_TYPES_H

#include <stdint.h>
#include <time.h>

typedef char *PVFS_string;
typedef uint8_t PVFS_boolean;
typedef int64_t PVFS_offset;
typedef int64_t PVFS_size;
typedef uint32_t PVFS_bitfield;
typedef int32_t PVFS_flag;
typedef int32_t PVFS_count32;
typedef int64_t PVFS_count64;
typedef uint64_t PVFS_volume_id;
typedef uint64_t PVFS_flow_id;
typedef int64_t PVFS_handle;
typedef int16_t PVFS_type;
typedef int32_t PVFS_fs_id;
typedef int32_t PVFS_error;
typedef int32_t PVFS_magic;
typedef uint32_t PVFS_uid;
typedef uint32_t PVFS_gid;
typedef uint32_t PVFS_permissions;
typedef int64_t PVFS_token;
typedef time_t PVFS_time;
typedef int32_t PVFS_msg_tag_t;

#define PVFS_NAME_MAX 256 /* Max length of PVFS filename */
#define PVFS_TOKEN_START 0 /* Token value for readdir */
#define PVFS_TOKEN_END 1
#define MAX_STRING_SIZE 1000

/* Pinode Number */
typedef struct
{
    int64_t handle;		/* unique identifier per PVFS2 file */
    PVFS_fs_id fs_id;		/* Filesystem ID */
}pinode_reference;

/* PVFS directory entry */
struct PVFS_dirent_s {
    /*pinode_number pinode_no;*/
    char d_name[PVFS_NAME_MAX + 1];
    /* something about how to get to the next one ? */
};
typedef struct PVFS_dirent_s PVFS_dirent;

/* PVFS_credentials structure */
struct PVFS_credentials_s {
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_permissions perms;
};
typedef struct PVFS_credentials_s PVFS_credentials;

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */

#endif /* __PVFS2_TYPES_H */
