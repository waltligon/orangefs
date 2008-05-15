/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _SECURITY_HASH_H_
#define _SECURITY_HASH_H_


#include <openssl/evp.h>

#include "pvfs2-types.h"


int SECURITY_hash_initialize(void);
int SECURITY_add_pubkey(PVFS_handle host, EVP_PKEY *pubkey);
EVP_PKEY *SECURITY_lookup_pubkey(PVFS_handle host);
void SECURITY_hash_finalize(void);


#endif /* _SECURITY_HASH_H_ */


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
