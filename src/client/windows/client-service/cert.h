/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

/*
 * Certificate support declarations 
 */

#ifndef __CERT_H
#define __CERT_H

#include <openssl/asn1.h>

#include "pvfs2.h"
#include "client-service.h"

void openssl_init();

void openssl_cleanup();

int get_proxy_cert_credential(HANDLE huser,
                              char *user_name,
                              PVFS_credential *credential,
                              ASN1_UTCTIME **expires);

int get_user_cert_credential(HANDLE huser,
                             char *user_name,
                             PVFS_credential *credential,
                             ASN1_UTCTIME **expires);


#endif