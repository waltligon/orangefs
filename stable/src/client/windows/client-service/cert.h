/*
 * (C) 2010-2011 Clemson University and Omnibond LLC
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

int get_cert_credentials(HANDLE huser,
                         char *userid,
                         PVFS_credentials *credentials,
                         ASN1_UTCTIME **expires);

#endif