/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _PINT_SECURITY_H_
#define _PINT_SECURITY_H_


#include "pvfs2-types.h"
#include "security-types.h"


/* nlmills: TODO: document these */
#define PINT_CAP_EXEC         (1 << 0)
#define PINT_CAP_WRITE        (1 << 1)
#define PINT_CAP_READ         (1 << 2)
#define PINT_CAP_SETATTR      (1 << 3)
#define PINT_CAP_CREATE       (1 << 4)
#define PINT_CAP_ADMIN        (1 << 5)
#define PINT_CAP_REMOVE       (1 << 6)
#define PINT_CAP_BATCH_CREATE (1 << 7)
#define PINT_CAP_BATCH_REMOVE (1 << 8)


int PINT_security_initialize(void);
int PINT_security_finalize(void);

int PINT_init_capability(PVFS_capability *cap);
int PINT_sign_capability(PVFS_capability *cap);
int PINT_verify_capability(const PVFS_capability *cap);

int PINT_init_credential(PVFS_credential *cred);
int PINT_sign_credential(PVFS_credential *cred);
int PINT_verify_credential(const PVFS_credential *cred);


#endif /* _PINT_SECURITY_H_ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
