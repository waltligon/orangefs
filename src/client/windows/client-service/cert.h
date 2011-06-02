/* Copyright (C) 2011 Omnibond, LLC
   Certificate support declarations */

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