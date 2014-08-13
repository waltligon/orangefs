/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _SECURITY_UTIL_H_
#define _SECURITY_UTIL_H_

#define IS_UNSIGNED_CRED(cred)    ((cred)->sig_size == 0)

char *PINT_print_op_mask(uint32_t op_mask, char *out_buf);

void PINT_null_capability(PVFS_capability *cap);
int PINT_capability_is_null(const PVFS_capability *cap);

PVFS_capability *PINT_dup_capability(const PVFS_capability *cap);
int PINT_copy_capability(const PVFS_capability *src, 
                         PVFS_capability *dest);
void PINT_debug_capability(const PVFS_capability *cap,
                           const char *prefix);
void PINT_cleanup_capability(PVFS_capability *cap);

PVFS_credential *PINT_dup_credential(const PVFS_credential *cred);
int PINT_copy_credential(const PVFS_credential *src,
                         PVFS_credential *dest);
void PINT_debug_credential(const PVFS_credential *cred,
                           const char *prefix,
                           PVFS_uid uid,
                           uint32_t num_groups,
                           const PVFS_gid *group_array);
void PINT_cleanup_credential(PVFS_credential *cred);

#ifdef WIN32
int PINT_get_security_path(const char *inpath, 
                           const char *userid, 
                           char *outpath, 
                           unsigned int outlen);
#endif

#endif /* _SECURITY_UTIL_H_ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
