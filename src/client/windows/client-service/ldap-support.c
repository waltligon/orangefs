/* Copyright (C) 2011 Omnibond LLC
   LDAP functions */

#include <Windows.h>

#include <ldap.h>
#include <ldap_ssl.h>

#include "ldap-support.h"

/* initialize LDAP SSL */
int PVFS_ldap_init()
{
    int ret;

    ret = ldapssl_client_init(NULL, NULL);

    if (ret == 0)
        ret = ldapssl_set_verify_mode(LDAPSSL_VERIFY_NONE);

    return ret;
}

/* cleanup LDAP SSL */
void PVFS_ldap_cleanup()
{
    ldapssl_client_deinit();
}



int get_ldap_credentials(char *userid,
                         PVFS_credentials *credentials)
{
    return 0;
}