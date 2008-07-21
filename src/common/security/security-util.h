/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _SECURITY_UTIL_H_
#define _SECURITY_UTIL_H_


#include "security-types.h"


/*  PINT_dup_capability
 *
 *  When passed a valid capability pointer this function will duplicate
 *  it and return the copy.  User must make sure to free both the new and
 *  old capabilities as normal.
 *	
 *  returns NULL on error
 *  returns valid PVFS_capability * on success
 */
PVFS_capability *PINT_dup_capability(const PVFS_capability* cap);

int PINT_copy_capability(const PVFS_capability* src, PVFS_capability* dest);

/*  PINT_release_capability
 *
 *  Frees any memory associated with a capability structure.
 *	
 *  no return value
 */
void PINT_release_capability(PVFS_capability* cap);

PVFS_credential *PINT_dup_credential(const PVFS_credential *cred);

void PINT_release_credential(PVFS_credential *cred);


#endif /* _SECURITY_UTIL_H_ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
