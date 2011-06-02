/* Copyright (C) 2011 Omnibond, LLC
   LDAP support declarations */

#ifndef __LDAP_SUPPORT_H
#define __LDAP_SUPPORT_H

#include "pvfs2.h"
#include "client-service.h"

int PVFS_ldap_init();

void PVFS_ldap_cleanup();

int get_ldap_credentials(char *userid,
                         PVFS_credentials *credentials);

#endif