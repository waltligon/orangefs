/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _SECURITY_TYPES_H_
#define _SECURITY_TYPES_H_


#include "pvfs2-types.h"


typedef unsigned char *PVFS_sig;

typedef struct PVFS_capability PVFS_capability;
struct PVFS_capability {
    PVFS_handle owner;
    PVFS_fs_id fsid;
    uint32_t sig_size;
    PVFS_sig signature;
    PVFS_time timeout;   /* seconds after epoch to time out */
    uint32_t op_mask;
    uint32_t num_handles;
    PVFS_handle *handle_array;
};

endecode_fields_2a2a_struct (
    PVFS_capability,
    PVFS_handle, owner,
    PVFS_fs_id, fsid,
    uint32_t, sig_size,
    PVFS_sig, signature,
    PVFS_time, timeout,
    uint32_t, op_mask,
    uint32_t, num_handles,
    PVFS_handle, handle_array)

typedef struct PVFS_credential PVFS_credential;
struct PVFS_credential {
    uint32_t serial;
    PVFS_uid userid;
    uint32_t num_groups;
    PVFS_gid *group_array;
    char * issuer_id;
    PVFS_time timeout;
    uint32_t sig_size;
    PVFS_sig signature;
};

endecode_fields_2aa1a_struct (
    PVFS_credential,
    uint32_t, serial,
    PVFS_uid, userid,
    uint32_t, num_groups,
    PVFS_gid, group_array,
    string, issuer_id,
    PVFS_time, timeout,
    uint32_t, sig_size,
    PVFS_sig, signature)


#endif /* _SECURITY_TYPES_H_ */


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
