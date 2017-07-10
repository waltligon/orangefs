/*                                                              
 * Copyright (C) 2012 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * Identity-mapping functions                                                                
 *
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pint-uid-map.h"

#include "pvfs2-debug.h"
#include "gossip.h"
#include "pint-security.h"
#include "security-util.h"

#ifdef ENABLE_SECURITY_CERT
#include <openssl/err.h>
#include <openssl/x509.h>

#include "pint-cert.h"
#include "cert-util.h"
#include "pint-ldap-map.h"

#ifdef ENABLE_CERTCACHE
#include "certcache.h"
#endif

extern X509_STORE *trust_store;
#endif

#if defined(ENABLE_SECURITY_CERT) && !defined(ENABLE_CERTCACHE)

/* return true if credential holds CA cert */
static int check_ca_cert(PVFS_credential *cred)
{
    int ret;
    X509 *xcert;
    X509_STORE_CTX *ctx;
    X509_NAME *subject;
    X509_OBJECT obj;

    if (cred == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* get the X509 certificate */
    ret = PINT_cert_to_X509(&cred->certificate, &xcert);
    if (ret != 0)
    {
        return ret;
    }

    /* create a X509_STORE_CTX for the trust store */
    ctx = X509_STORE_CTX_new();
    if (ctx == NULL)
    {
        X509_free(xcert);
        return -PVFS_ESECURITY;
    }

    /* load trust store into context */
    if (!X509_STORE_CTX_init(ctx, trust_store, NULL, NULL))
    {
        X509_STORE_CTX_free(ctx);
        X509_free(xcert);
        return -PVFS_ESECURITY;
    }

    /* get cert subject */
    if ((subject = X509_get_subject_name(xcert)) == NULL)
    {
        X509_STORE_CTX_cleanup(ctx);
        X509_STORE_CTX_free(ctx);
        X509_free(xcert);
        return -PVFS_ESECURITY;
    }

    /* search trust store by subject */
    ret = X509_STORE_get_by_subject(ctx, X509_LU_X509, subject, &obj);

    X509_STORE_CTX_cleanup(ctx);
    X509_STORE_CTX_free(ctx);
    X509_free(xcert);

    return ret;
}
#endif

/* map credential to uid/group list */
int PINT_map_credential(PVFS_credential *cred, 
                        PVFS_uid *uid,
                        uint32_t *num_groups,
                        PVFS_gid *group_array)
{
    int ret = 0; 
#ifdef ENABLE_CERTCACHE
    seccache_entry_t *entry;
    certcache_data_t *data;
#endif

    if (cred == NULL || uid == NULL || num_groups == NULL)
    {
        return -PVFS_EINVAL;
    }

#ifdef ENABLE_SECURITY_CERT

    /* do not map unsigned credential (fields won't be used) */
    if (IS_UNSIGNED_CRED(cred))
    {
        *uid = PVFS_UID_MAX;
        *num_groups = 1;
        group_array[0] = PVFS_GID_MAX;

        return 0;
    }

#ifdef ENABLE_CERTCACHE
    /* check certificate cache -- note: CA cert is cached as root */
    entry = PINT_certcache_lookup(&cred->certificate);
    if (entry != NULL)
    {        
        /* cache hit */
        data = (certcache_data_t *) entry->data;

        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: certificate cache hit (%s)\n",
                     __func__, data->subject);
        *uid = data->uid;
        *num_groups = data->num_groups;
        memcpy(group_array, data->group_array, 
               data->num_groups * sizeof(PVFS_gid));
        ret = 0;
    }
    else {
        ret = PINT_ldap_map_credential(cred, uid, num_groups, group_array);
        /* cache certificate info */
        if (ret == 0)
        {
            ret = PINT_certcache_insert(&cred->certificate, *uid, 
                                              *num_groups, group_array);
            if (ret < 0)
            {
                /* issue warning */
                gossip_err("Warning: could not cache certificate\n");
                ret = 0;
            }
        }
    }
#else /* ENABLE_CERTCACHE */
    /* if provided certificate is the CA certificate, map to root user 
     * this is used primarily when creating a new file system 
     * note that the credential must have been signed by the CA private key
     */
    ret = check_ca_cert(cred);
    if (ret > 0)
    {
        *uid = 0;
        *num_groups = 1;
        group_array[0] = 0;

        gossip_debug(GOSSIP_SECURITY_DEBUG, "Mapped credential to root\n");

        return 0;
    }
    else if (ret < 0)
    {
        /* report error and continue */
        PINT_security_error(__func__, ret);
    }

    /* backend for cert mapping is LDAP */
    ret = PINT_ldap_map_credential(cred, uid, num_groups, group_array);
#endif  /* ENABLE_CERTCACHE */

#else /* ENABLE_SECURITY_CERT */
    /* return info in credential */
    *uid = cred->userid;
    *num_groups = cred->num_groups;
    memcpy(group_array, cred->group_array, 
           cred->num_groups * sizeof(PVFS_gid));
#endif /* ENABLE_SECURITY_CERT */

    /* return -PVFS_EINVAL if no groups */
    if (ret == 0 && num_groups == 0)
    {
        ret = -PVFS_EINVAL;
    }
    
    return ret;
}


