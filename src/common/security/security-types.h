/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _SECURITY_TYPES_H_
#define _SECURITY_TYPES_H_


#include "pvfs2-types.h"


typedef unsigned char *PVFS_signature;

/* nlmills: TODO: link to capability documentation */
typedef struct PVFS_capability PVFS_capability;
struct PVFS_capability {
    char *issuer;              /* alias of the issuing server */
    PVFS_fs_id fsid;           /* fsid for which this capability is valid */
    uint32_t sig_size;         /* length of the signature in bytes */
    PVFS_signature signature;        /* digital signature */
    PVFS_time timeout;         /* seconds after epoch to time out */
    uint32_t op_mask;          /* allowed operations mask */
    uint32_t num_handles;      /* number of elements in the handle array */
    PVFS_handle *handle_array; /* handles in this capability */
};
endecode_fields_3a2a_struct (
    PVFS_capability,
    string, issuer,
    PVFS_fs_id, fsid,
    skip4,,
    uint32_t, sig_size,
    PVFS_signature, signature,
    PVFS_time, timeout,
    uint32_t, op_mask,
    uint32_t, num_handles,
    PVFS_handle, handle_array)

/* nlmills: TODO: link to credential documentation */
typedef struct PVFS_credential PVFS_credential;
struct PVFS_credential {
    PVFS_fs_id fsid;       /* fsid for which this credential is valid */
    uint32_t serial;       /* serial number for use in revocation */
    PVFS_uid userid;       /* user id */
    uint32_t num_groups;   /* length of group_array */
    PVFS_gid *group_array; /* groups for which the user is a member */
    char *issuer;          /* alias of the issuing server */
    PVFS_time timeout;     /* seconds after epoch to time out */
    uint32_t sig_size;     /* length of the signature in bytes */
    PVFS_signature signature;    /* digital signature */
};
endecode_fields_3a2a_struct (
    PVFS_credential,
    PVFS_fs_id, fsid,
    uint32_t, serial,
    PVFS_uid, userid,
    uint32_t, num_groups,
    PVFS_gid, group_array,
    string, issuer,
    PVFS_time, timeout,
    uint32_t, sig_size,
    PVFS_signature, signature)


#endif /* _SECURITY_TYPES_H_ */


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
