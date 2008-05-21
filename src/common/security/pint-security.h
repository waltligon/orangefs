/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#ifndef PINT_SECURITY_H
#define PINT_SECURITY_H


#include "pvfs2-config.h"
#include "pvfs2-types.h"


typedef unsigned char *PVFS_sig;

/* TODO: encode and decode PVFS_sig */

typedef struct PVFS_capability PVFS_capability;
struct PVFS_capability {
        PVFS_handle owner;
        PVFS_fs_id fsid;
        PVFS_sig signature;
        uint32_t sig_size;
	PVFS_time timeout;   /* seconds after epoch to time out */
	uint32_t op_mask;
	uint32_t num_handles;
	PVFS_handle *handle_array;
};

/*endecode_fields_3a (
		PVFS_capability,
		PVFS_sig, signature,
		PVFS_time, timeout,
		uint32_t, op_mask,
		uint32_t, num_handles,
		PVFS_handle, handle_array)*/

/*typedef struct PVFS_credentials PVFS_credentials;
struct PVFS_credentials {
	PVFS_sig signature;
	PVFS_time timeout;
	PVFS_uid userid;
	uint32_t num_groups;
	PVFS_gid *group_array;
};*/

/*endecode_fields_3a (
	PVFS_credentials,
	PVFS_sig, signature,
	PVFS_time, timeout,
	PVFS_uid, userid,
	uint32_t, num_groups,
	PVFS_gid, group_array)*/

/*  top-level security functions */

/*  PINT_security_initialize	
 *
 *  Initializes the security module
 *	
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_EIO if key file is missing or invalid
 *  returns 0 on sucess
 */
int PINT_security_initialize(void);

/*  PINT_security_finalize	
 *
 *  Finalizes the security module
 *	
 *  returns PVFS_EALREADY if already finalized
 *  returns 0 on sucess
 */
int PINT_security_finalize(void);

/* creates a signature from the remaining fields
 * any existing signature is overwritten
 */
int PINT_sign_capability(PVFS_capability *);

/* computes a signature from the fields and compares 
 * to the existing signature returns non-zero if equal
 * nothing changed in the structure
 */
int PINT_verify_capability(PVFS_capability *);

/* creates a signature from the remaining fields
 * any existing signature is overwritten
 */
void PINT_sign_credentials (PVFS_credentials *);

/* computes a signature from the fields and compares 
 * to the existing signature returns non-zero if equal
 * nothing changed in the structure
 */
int PINT_verify_credentials (PVFS_credentials *);


#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
