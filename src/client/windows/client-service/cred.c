/*
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * Windows credential functions
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <crypto/evp.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "pint-util.h"

#include "client-service.h"
#include "cred.h"

extern PORANGEFS_OPTIONS goptions;

/* get credential for the "SYSTEM" user */
int get_system_credential(PVFS_credential *credential)
{
    int ret = 0;
    PVFS_gid group_array[] = { 0 };

    /* fill in "root" credential for client-side user mapping */
    ret = init_credential(0, group_array, 1, NULL, NULL, credential);

    return ret;
}

/* sign credential using specified PEM private key file */
int sign_credential(const char *key_file, 
                    PVFS_credential *cred)
{
    FILE *fkey;
    EVP_PKEY *privkey;
    BOOL key_flag = FALSE;
    const EVP_MD *md;
    EVP_MD_CTX *mdctx;
    int ret = 0, err;

    client_debug("    sign_credential: enter\n");

    if (key_file == NULL || cred == NULL)
    {
        report_error("Credential signing error: ", -PVFS_EINVAL);
        return -PVFS_EINVAL;
    }

    if (goptions->security_mode == SECURITY_MODE_KEY)
    {
        privkey = (EVP_PKEY *) goptions->private_key;
    }
    else
    {
        /* read in client private key */
        fkey = fopen(key_file, "r");
        if (fkey == NULL)
        {
            err = errno;
            report_error("Credential signing error (key): ", 
                -PVFS_errno_to_error(err));
            return err;
        }

        privkey = PEM_read_PrivateKey(fkey, NULL, NULL, NULL);

        fclose(fkey);

        if (privkey == NULL)
        {
            report_error("Credential signing error:", -PVFS_ESECURITY);
            return -PVFS_ESECURITY;
        }

        key_flag = TRUE;
    }

    /* sign credential using private key */
    cred->signature = (PVFS_signature) malloc(EVP_PKEY_size(privkey));
    if (cred->signature == NULL)
    {
        report_error("Credential signing error: ", -PVFS_ENOMEM);
        if (key_flag)
        {
            EVP_PKEY_free(privkey);
        }
        return -PVFS_ENOMEM;
    }

    md = EVP_sha1();
    mdctx = EVP_MD_CTX_new();

    ret = EVP_SignInit_ex(mdctx, md, NULL);
    ret &= EVP_SignUpdate(mdctx, &cred->userid, sizeof(PVFS_uid));
    ret &= EVP_SignUpdate(mdctx, &cred->num_groups, sizeof(uint32_t));
    ret &= EVP_SignUpdate(mdctx, cred->group_array, 
                          cred->num_groups * sizeof(PVFS_gid));
    ret &= EVP_SignUpdate(mdctx, cred->issuer, 
                          strlen(cred->issuer) * sizeof(char));
    ret &= EVP_SignUpdate(mdctx, &cred->timeout, sizeof(PVFS_time));
    if (!ret)
    {
        ret = -PVFS_ESECURITY;
        goto sign_credential_error;
    }
    ret = EVP_SignFinal(mdctx, cred->signature, &cred->sig_size, privkey);
    if (!ret)
    {
        ret = -PVFS_ESECURITY;
        goto sign_credential_error;
    }

    ret = 0;

    goto sign_credential_exit;

sign_credential_error:

    report_error("Credential signing error: ", -PVFS_ESECURITY);
    free(cred->signature);

sign_credential_exit:

    EVP_MD_CTX_free(mdctx);
    if (key_flag)
    {
        EVP_PKEY_free(privkey);
    }

    client_debug("    sign_credential: exit (%d)\n", ret);

    return ret;
}

/* initialize and sign credential - credential must be allocated */ 
int init_credential(PVFS_uid uid,
                    PVFS_gid group_array[],
                    uint32_t num_groups,
                    const char *key_file,
                    const PVFS_certificate *cert,
                    PVFS_credential *cred)
{
    int ret = 0;

    client_debug("    init_credential: enter\n");

    if (group_array == NULL || cred == NULL || num_groups == 0)
    {
        report_error("Credential error:", -PVFS_EINVAL);
        return -PVFS_EINVAL;
    }

    memset(cred, 0, sizeof(PVFS_credential));    

    /* fill in issuer */
    cred->issuer = (char *) malloc(PVFS_REQ_LIMIT_ISSUER);
    if (!cred->issuer)
    {
        report_error("   init_credential:", -PVFS_ENOMEM);
        return -PVFS_ENOMEM;
    }
    strcpy(cred->issuer, "C:");
    if (gethostname(cred->issuer+2, PVFS_REQ_LIMIT_ISSUER-3) == SOCKET_ERROR)
    {
        char errbuf[256];
		int err;

		err = WSAGetLastError();
        _snprintf(errbuf, sizeof(errbuf), "    init_credential (gethostname): %d", 
            err);
        report_error(errbuf, err);
        free(cred->issuer);
        return -PVFS_ENONET;
    }

    /* fill in uid/groups for non-cert modes */
    cred->group_array = (PVFS_gid *) malloc(sizeof(PVFS_gid) * num_groups);
    if (!cred->group_array)
    {
        report_error("   init_credential:", -PVFS_ENOMEM);
		free(cred->issuer);
        return -PVFS_ENOMEM;
    }

    /* set groups and uid */
    cred->num_groups = num_groups;
    memcpy(cred->group_array, group_array, sizeof(PVFS_gid) * num_groups);
    cred->userid = uid;

    /* TODO: revise caching (use server timeout setting) */
    cred->timeout = time(NULL) + PVFS2_SECURITY_TIMEOUT_MAX;

    /* append certificate if necessary */
    if (cert != NULL)
    {
        cred->certificate.buf = (PVFS_cert_data) malloc(cert->buf_size);
        if (cred->certificate.buf == NULL)
        {
            cleanup_credential(cred);
            return -PVFS_ENOMEM;
        }

        memcpy(cred->certificate.buf, cert->buf, cert->buf_size);
        cred->certificate.buf_size = cert->buf_size;
    }
  
    /* sign credential depending on mode */
    if (goptions->security_mode == SECURITY_MODE_KEY)
    {
        if ((ret = sign_credential((key_file != NULL) ? 
                                    key_file : 
                                    goptions->key_file, cred)) != 0)
        {
            cleanup_credential(cred);
            return ret;
        }
    }
    else if (goptions->security_mode == SECURITY_MODE_CERT)
    {
        if ((ret = sign_credential(key_file, cred)) != 0)
        {
            cleanup_credential(cred);
            return ret;
        }
    }

    client_debug("    init_credential: exit (%d)\n", ret);

    return ret;
}

/* free credential fields - caller must free credential */
void cleanup_credential(PVFS_credential *cred)
{
    if (cred) 
    {
        if (cred->group_array)
        {
            free(cred->group_array);
            cred->group_array = NULL;
        }
        if (cred->signature)
        {
            free(cred->signature);
            cred->signature = NULL;
        }
        if (cred->issuer)
        {
            free(cred->issuer);
            cred->issuer = NULL;
        }

        cred->num_groups = 0;
        cred->sig_size = 0;
    }
}

/* check if credential is a member of group */
int credential_in_group(PVFS_credential *cred, PVFS_gid group)
{
    unsigned int i;

    for (i = 0; i < cred->num_groups; i++)
    {
        if (cred->group_array[i] == group)
        {
            return 1;
        }
    }

    return 0;
}

/* add group to credential group list */
/* TODO: remove? */
#if 0
void credential_add_group(PVFS_credential *cred, PVFS_gid group)
{
    PVFS_gid *group_array;
    unsigned int i;

    if (cred->num_groups > 0)
    {
        /* copy existing group array and append group */
        cred->num_groups++;
        group_array = (PVFS_gid *) malloc(sizeof(PVFS_gid) * cred->num_groups);
        for (i = 0; i < cred->num_groups - 1; i++)
        {
            group_array[i] = cred->group_array[i];
        }
        group_array[cred->num_groups-1] = group;

        free(cred->group_array);
        cred->group_array = group_array;
    }
    else
    {
        /* add one group */
        cred->group_array = (PVFS_gid *) malloc(sizeof(PVFS_gid));
        cred->group_array[0] = group;
        cred->num_groups = 1;
    }
}

/* set the credential timeout to current time + timeout
   use PVFS2_DEFAULT_CREDENTIAL_TIMEOUT */
void credential_set_timeout(PVFS_credential *cred, PVFS_time timeout)
{
    cred->timeout = PINT_util_get_current_time() + timeout;
}
#endif
