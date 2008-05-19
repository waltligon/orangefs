/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _SECURITY_HASH_H_
#define _SECURITY_HASH_H_


#include <openssl/evp.h>	// encryption library

#include "pvfs2-types.h"


/*  SECURITY_hash_initialize
 *
 *  Initializes the hash table for use
 *
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_ENOMEM if memory cannot be allocated
 *  returns 0 on success
 */
int SECURITY_hash_initialize(void);

/*  SECURITY_hash_finalize
 *
 *  Frees everything allocated within the table
 *  and anything used to set it up
 *
 *  returns nothing
 */
void SECURITY_hash_finalize(void);

/*  SECURITY_add_pubkey
 *
 *  Takes an EVP_PKEY and inserts it into the hash table
 *  based on the host ID.  If the host ID already
 *  exists in the table, it's corresponding key is replaced 
 *  with the new one
 *
 *  returns PVFS_ENOMEM if memory cannot be allocated
 *  returns 0 on success
 */
int SECURITY_add_pubkey(uint32_t host, EVP_PKEY *pubkey);

/*  SECURITY_lookup_pubkey
 *
 *  Takes a host ID and returns a pointer to the
 *  matching EVP_PKEY structure
 *
 *  returns NULL if no matching key is found
 */
EVP_PKEY *SECURITY_lookup_pubkey(uint32_t host);


#endif /* _SECURITY_HASH_H_ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
 
