/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#ifndef PINT_SECURITY_H
#define PINT_SECURITY_H


#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "security-types.h"


#define PINT_CAP_EXEC    (1 << 0)
#define PINT_CAP_WRITE   (1 << 1)
#define PINT_CAP_READ    (1 << 2)
#define PINT_CAP_SETATTR (1 << 3)
#define PINT_CAP_CREATE  (1 << 4)
#define PINT_CAP_ADMIN   (1 << 5)
#define PINT_CAP_REMOVE  (1 << 6)


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

int PINT_verify_certificate(const char *, const unsigned char *, unsigned int);

const char *PINT_lookup_account(const char *);

int PINT_lookup_userid(const char *, PVFS_uid *);

int PINT_lookup_groups(const char *, PVFS_gid **, uint32_t *);

/* creates a signature from the remaining fields
 * any existing signature is overwritten
 */
int PINT_sign_capability(PVFS_capability *);

/* computes a signature from the fields and compares 
 * to the existing signature returns non-zero if equal
 * nothing changed in the structure
 */
int PINT_verify_capability(PVFS_capability *);

/* computes a signature from the fields and compares 
 * to the existing signature returns non-zero if equal
 * nothing changed in the structure
 */
int PINT_verify_credential (PVFS_credential *);

/*  PINT_init_capability
 *
 *  Function to call after creating an initial capability
 *  structure to initialize needed memory space for the signature.
 *  Sets all fields to 0 or NULL to be safe
 *	
 *  returns -PVFS_ENOMEM on error
 *  returns -PVFS_EINVAL if passed an invalid structure
 *  returns 0 on success
 */
int PINT_init_capability(PVFS_capability *);


#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
