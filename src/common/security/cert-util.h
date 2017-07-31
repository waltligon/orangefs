/*                                                              
 * Copyright (C) 2012 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * Certificate declarations                                                                
 *
*/

#ifndef _CERT_UTIL_H_
#define _CERT_UTIL_H_

#include <openssl/evp.h>
#include <openssl/x509.h>

#include "pvfs2-config.h"
#include "pvfs2-types.h"

/* load an X509 certificate struct from the specified file */
int PINT_load_cert_from_file(const char *path,
                             X509 **cert);

/* load a key struct from the specified file */
int PINT_load_key_from_file(const char *path,
                            EVP_PKEY **key);

/* save an X509 certificate to disk */
int PINT_save_cert_to_file(const char *path,
                           X509 *cert);

/* save a public key struct to disk */
int PINT_save_pubkey_to_file(const char *path,
                          EVP_PKEY *key);

/* save a public key struct to disk */
int PINT_save_privkey_to_file(const char *path,
                          EVP_PKEY *key);

/* convert a PVFS_certificate to an X509 struct */
int PINT_cert_to_X509(const PVFS_certificate *cert, 
                      X509 **xcert);

/* convert an X509 struct to a PVFS_certificate */
int PINT_X509_to_cert(const X509 *xcert, 
                      PVFS_certificate **cert);

/* copy PVFS_certificate */
int PINT_copy_cert(const PVFS_certificate *src,
                   PVFS_certificate *dest);

/* copy security key */
int PINT_copy_key(const PVFS_security_key *src,
                  PVFS_security_key *dest);

/* free PVFS_certificate memory */
void PINT_cleanup_cert(PVFS_certificate *cert);

/* free security key memory */
void PINT_cleanup_key(PVFS_security_key *key);

#endif
