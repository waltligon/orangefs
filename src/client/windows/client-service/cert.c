/* Copyright (C) 2011 Omnibond LLC
   Certificate functions */

#include <Windows.h>
#include <Userenv.h>
#include <stdio.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

#include "cert.h"
#include "user-cache.h"

#define OPENSSL_CERT_ERROR    0xFFFF

extern PORANGEFS_OPTIONS goptions;

/* initialize OpenSSL */
void openssl_init()
{
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
}

/* cleanup OpenSSL */
void openssl_cleanup()
{
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();
    ERR_remove_state(0);
}

/* load certificate from file (PEM format) */
static unsigned long load_cert_from_file(char *path, 
                                         X509 **cert)
{
    FILE *f;

    if (path == NULL || cert == NULL)
        return -1;

    f = fopen(path, "r");
    if (f == NULL)
        return errno;

    *cert = PEM_read_X509(f, NULL, NULL, NULL);
    if (*cert == NULL)
        return OPENSSL_CERT_ERROR;

    fclose(f);

    return 0;
}


static int get_proxy_auth_ex_data_idx(void)
{
    static volatile int idx = -1;
    if (idx < 0)
    {
        CRYPTO_w_lock(CRYPTO_LOCK_X509_STORE);
        if (idx < 0)
        {
            idx = X509_STORE_CTX_get_ex_new_index(0,
                                                  "for verify callback",
                                                  NULL,NULL,NULL);
        }
        CRYPTO_w_unlock(CRYPTO_LOCK_X509_STORE);
    }

    return idx;
}

/* parse the credential string uid/gid from credstr */
static int parse_credentials(char *credstr, PVFS_uid *uid, PVFS_gid *gid)
{
    char *p, uidstr[16], gidstr[16];
    int i, ret = 0;

    uidstr[0] = gidstr[0] = '\0';
    i = 0;
    p = credstr;
    while (*p && *p != '/' && i < 15)
    {
        if (isdigit(*p))
        {
            uidstr[i++] = *p++;
        }
        else 
        {
            /* error */
            ret = 1;
            break;
        }
    }
    uidstr[i] = '\0';
    if (ret == 0)
    {
        if (*p == '/')
            p++;
        i = 0;
        while(*p && i < 15)
        {
            if (isdigit(*p))
            {
                gidstr[i++] = *p++;
            }
            else 
            {
                ret = 1;
                break;
            }
        }
        gidstr[i] = '\0';
    }

    if (ret == 0)
    {
        *uid = atoi(uidstr);
        *gid = atoi(gidstr);
    }

    return ret;
}

static int verify_callback(int ok, X509_STORE_CTX *ctx)
{
    X509 *xs;
    PROXY_CERT_INFO_EXTENSION *pci;
    char *credstr;
    PVFS_credentials *credentials;
    int ret;

    /* prior verifies have succeeded */
    if (ok == 1) 
    {
        /* parse the credential string uid/gid from the policy */
        xs = ctx->current_cert;
        if (xs->ex_flags & EXFLAG_PROXY)
        {
            pci = (PROXY_CERT_INFO_EXTENSION *) 
                    X509_get_ext_d2i(xs, NID_proxyCertInfo, NULL, NULL);

            credstr = (char *) pci->proxyPolicy->policy->data;
            if (pci->proxyPolicy->policy->length > 0)
            {
                credentials = (PVFS_credentials *) X509_STORE_CTX_get_ex_data(
                                 ctx, get_proxy_auth_ex_data_idx());
                ret = parse_credentials(credstr, &credentials->uid, 
                                        &credentials->gid);
                if (ret != 0)
                {
                    DbgPrint("   verify_cert: could not parse credential string: %s\n", credstr);
                    ok = 0;
                }
            }
            else
            {
                DbgPrint("    verify_cert: could not load policy\n");
                ok = 0;
            }

            PROXY_CERT_INFO_EXTENSION_free(pci);
        }
    }
    
    return ok;
}

/* verify certificate */
static unsigned long verify_cert(X509 *cert, 
                                 X509 *ca_cert,
                                 STACK_OF(X509) *chain,
                                 PVFS_credentials *credentials)
{
    X509_STORE *trust_store;
    X509_STORE_CTX *ctx;
    int ret, verify_flag = 0;
    int (*save_verify_cb)(int ok, X509_STORE_CTX *ctx);

    /* add CA cert to trusted store */
    trust_store = X509_STORE_new();
    if (trust_store == NULL)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    ret = X509_STORE_add_cert(trust_store, ca_cert);
    if (!ret)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    /* setup the context with the certs */
    ctx = X509_STORE_CTX_new();
    if (ctx == NULL)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    ret = X509_STORE_CTX_init(ctx, trust_store, cert, chain);
    if (!ret)
    {
        ret = OPENSSL_CERT_ERROR;
        goto verify_cert_exit;
    }

    /* set up verify callback */
    save_verify_cb = ctx->verify_cb;
    X509_STORE_CTX_set_verify_cb(ctx, verify_callback);
    X509_STORE_CTX_set_ex_data(ctx, get_proxy_auth_ex_data_idx(), credentials);
    X509_STORE_CTX_set_flags(ctx, X509_V_FLAG_ALLOW_PROXY_CERTS);

    /* verify the cert */
    verify_flag = 1;
    ret = (X509_verify_cert(ctx) == 1) ? 0 : OPENSSL_CERT_ERROR;

    X509_STORE_CTX_set_verify_cb(ctx, save_verify_cb);
    
verify_cert_exit:

    if (verify_flag && ret == OPENSSL_CERT_ERROR)
    {
        DbgPrint("   verify_cert: %s\n", 
            X509_verify_cert_error_string(ctx->error));
    }

    if (ctx != NULL)
    {
        X509_STORE_CTX_cleanup(ctx);
        X509_STORE_CTX_free(ctx);
    }

    if (trust_store != NULL)
    {
        X509_STORE_free(trust_store);
    }

    return ret;
}

/* get user profile directory -- profile_dir should be MAX_PATH bytes */
static unsigned int get_profile_dir(HANDLE huser, 
                                    char *profile_dir)
{
    DWORD profile_len = MAX_PATH;

    if (!GetUserProfileDirectory(huser, profile_dir, &profile_len))
        return GetLastError();

    return 0;
}

/* retrieve OrangeFS credentials from cert */
int get_cert_credentials(HANDLE huser,
                         char *userid,
                         PVFS_credentials *credentials,
                         ASN1_UTCTIME **expires)
{
    char cert_dir[MAX_PATH], cert_path[MAX_PATH],
         cert_pattern[MAX_PATH];
    HANDLE h_find;
    WIN32_FIND_DATA find_data;
    X509 *cert = NULL, *chain_cert = NULL, *ca_cert = NULL;
    STACK_OF(X509) *chain = NULL;
    int ret;
    time_t now;
    unsigned long err;
    char errstr[256];

    DbgPrint("   get_cert_credentials: enter\n");
    
    if (userid == NULL || credentials == NULL || expires == NULL)
    {
        DbgPrint("   get_cert_credentials: invalid parameter\n");
        return -1;
    }

    /* locate the certificates and CA */
    if (strlen(goptions->cert_dir_prefix) > 0)
    {
        if ((strlen(goptions->cert_dir_prefix) + strlen(userid) + 8) > MAX_PATH)
        {
            DbgPrint("   get_cert_credentials: user %s: path to cert too long\n", userid);
            return -1;
        }

        /* cert dir is cert_dir_prefix\userid */
        strcpy(cert_dir, goptions->cert_dir_prefix);
        strcat(cert_dir, userid);
    }
    else
    {
        /* get profile directory */
        ret = get_profile_dir(huser, cert_dir);
        if (ret == 0)
        {
            if (strlen(cert_dir) > 0 && cert_dir[strlen(cert_dir)-1] != '\\')
                strcat(cert_dir, "\\");
        }
        else
        {
            DbgPrint("   get_cert_credentials: user %s: could not locate profile dir: %d\n", userid,
                ret);
            return ret;
        }
        
        if (strlen(cert_dir) + 7 > MAX_PATH)
        {
            DbgPrint("   get_cert_credentials: user %s: profile dir too long\n", userid);
            return -1;
        }
    }
    
    /* load certs */
    chain = sk_X509_new_null();

    strcpy(cert_pattern, cert_dir);
    strcat(cert_pattern, "cert.*");
    h_find = FindFirstFile(cert_pattern, &find_data);
    if (h_find == INVALID_HANDLE_VALUE)
    {
        DbgPrint("   get_cert_credentials: user %s: no certificates\n", userid);
        ret = -1;
        goto get_cert_credentials_exit;
    }

    do
    {
        strcpy(cert_path, cert_dir);        
        strcat(cert_path, find_data.cFileName);
        /* load proxy cert */
        if (!stricmp(find_data.cFileName, "cert.0"))
        {
            ret = load_cert_from_file(cert_path, &cert);
        }
        else
        {
            /* load intermediate certs (including user cert) */
            ret = load_cert_from_file(cert_path, &chain_cert);
            if (ret == 0) 
                sk_X509_push(chain, chain_cert);
        }
        if (ret != 0)
        {
            DbgPrint("   get_cert_credentials: error loading cert %s: %d\n", 
                cert_path, ret);
        }
    } while (ret == 0 && FindNextFile(h_find, &find_data));

    FindClose(h_find);

    /* no proxy cert */
    if (cert == NULL)
    {
        DbgPrint("   get_cert_credentials: missing or invalid cert.0\n");
        ret = OPENSSL_CERT_ERROR;
    }
    
    if (ret != 0)
        goto get_cert_credentials_exit;

    /* load CA cert */
    ret = load_cert_from_file(goptions->ca_path, &ca_cert);
    if (ret != 0)
    {
        DbgPrint("   get_cert_credentials: error loading CA cert %s: %d\n", 
            goptions->ca_path, ret);
        goto get_cert_credentials_exit;
    }

    /* read and cache credentials from certificate */
    ret = verify_cert(cert, ca_cert, chain, credentials);

    if (ret == 0)
    {        
        *expires = M_ASN1_UTCTIME_dup(X509_get_notAfter(cert));        
    }

get_cert_credentials_exit:

    /* error handling */
    if (ret == OPENSSL_CERT_ERROR)
    {
        while ((err = ERR_get_error()) != 0)
        {
            ERR_error_string_n(err, errstr, 256);
            DbgPrint("   get_cert_credentials: %s\n", errstr);
        }
    }

    /* free chain */
    if (chain != NULL)
        sk_X509_pop_free(chain, X509_free);

    if (cert != NULL)
        X509_free(cert);
    if (ca_cert != NULL)
        X509_free(ca_cert);

    DbgPrint("   get_cert_credentials: exit\n");

    return ret;
}
