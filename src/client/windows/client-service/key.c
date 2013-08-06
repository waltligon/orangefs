/*
 * Copyright (C) 2013 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>

#include <openssl/pem.h>

#include "client-service.h"
#include "key.h"

extern PORANGEFS_OPTIONS goptions;

int key_sign_credential(PVFS_credential *cred)
{
    FILE *fkey;
    EVP_PKEY *privkey;
    const EVP_MD *md;
    EVP_MD_CTX mdctx;
    int ret;

    /* TODO: cache key in goptions */

    /* read in client private key */
    fkey = fopen(goptions->key_file, "rb");
    if (fkey == NULL)
    {
        return -PVFS_ENOENT;
    }

    privkey = PEM_read_PrivateKey(fkey, NULL, NULL, NULL);

    fclose(fkey);

    if (privkey == NULL)
    {
        /* TODO */
        return -PVFS_ESECURITY;
    }

    /* sign credential using private key */
    cred->signature = (PVFS_signature) malloc(EVP_PKEY_size(privkey));
    if (cred->signature == NULL)
    {
        EVP_PKEY_free(privkey);
        return -PVFS_ENOMEM;
    }

    md = EVP_sha1();
    EVP_MD_CTX_init(&mdctx);

    ret = EVP_SignInit_ex(&mdctx, md, NULL);
    ret &= EVP_SignUpdate(&mdctx, &cred->userid, sizeof(PVFS_uid));
    ret &= EVP_SignUpdate(&mdctx, &cred->num_groups, sizeof(uint32_t));
    ret &= EVP_SignUpdate(&mdctx, cred->group_array, 
                          cred->num_groups * sizeof(PVFS_gid));
    ret &= EVP_SignUpdate(&mdctx, cred->issuer, 
                          strlen(cred->issuer) * sizeof(char));
    ret &= EVP_SignUpdate(&mdctx, &cred->timeout, sizeof(PVFS_time));
    if (!ret)
    {
        /* TODO */
        free(cred->signature);
        EVP_MD_CTX_cleanup(&mdctx);
        EVP_PKEY_free(privkey);
        return -PVFS_ESECURITY;
    }
    ret = EVP_SignFinal(&mdctx, cred->signature, &cred->sig_size, privkey);
    if (!ret)
    {
        /* TODO */
        free(cred->signature);
        EVP_MD_CTX_cleanup(&mdctx);
        EVP_PKEY_free(privkey);
        return -PVFS_ESECURITY;
    }

    EVP_MD_CTX_cleanup(&mdctx);
    EVP_PKEY_free(privkey);

    return 0;
}
