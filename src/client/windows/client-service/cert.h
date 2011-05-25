/* Copyright (C) 2011 Omnibond, LLC
   Certificate support declarations */

#include "pvfs2.h"
#include "client-service.h"

void openssl_init();

void openssl_cleanup();

int get_cert_credentials(HANDLE huser,
                         char *userid,
                         PVFS_credentials *credentials,
                         time_t *expires);
