/* Copyright (C) 2011 Omnibond LLC
   Certificate functions */

#include <Windows.h>
#include <LM.h>
#include <stdio.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#include "pvfs2.h"

extern char *convert_wstring(const wchar_t *);
extern wchar_t *convert_mbstring(const char *);

/* initialize OpenSSL */
static void openssl_init()
{
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
}

/* cleanup OpenSSL */
static void openssl_cleanup()
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
    if (cert == NULL)
        return ERR_get_error();

    return 0;
}

/* verify certificate */
static unsigned long verify_cert(X509 *cert, 
                                 X509 *ca_cert)
{
    X509_STORE *trust_store;
    X509_STORE_CTX *ctx;
    int ret;
    unsigned long err;

    /* add CA cert to trusted store */
    trust_store = X509_STORE_new();
    if (trust_store == NULL)
        goto verify_cert_exit;

    ret = X509_STORE_add_cert(trust_store, ca_cert);
    if (!ret)
        goto verify_cert_exit;

    /* setup the context with the certs */
    ctx = X509_STORE_CTX_new();
    if (ctx == NULL)
        goto verify_cert_exit;

    ret = X509_STORE_CTX_init(ctx, trust_store, cert, NULL);
    if (!ret)
        goto verify_cert_exit;

    /* TODO: verify proxy cert */
    /* verify the cert */
    X509_verify_cert(ctx);
    
verify_cert_exit:
    err = ERR_get_error();

    if (ctx != NULL)
    {
        X509_STORE_CTX_cleanup(ctx);
        X509_STORE_CTX_free(ctx);
    }

    if (trust_store != NULL)
    {
        X509_STORE_free(trust_store);
    }

    return err;
}

/* get user profile directory */
static unsigned int get_profile_dir(char *userid, 
                                    char *profile_dir)
{
    USER_INFO_4 user_info;
    LPCWSTR wuserid;
    int ret;
    char *mbstr;

    /* convert to unicode */
    wuserid = convert_mbstring(userid);
    if (wuserid == NULL)
        return -1;

    /* get user information */
    ret = NetUserGetInfo(NULL, wuserid, 4, &user_info);

    if (ret == 0)
    {
        mbstr = convert_wstring(user_info.usri4_profile);
        if (mbstr == NULL) 
        {
            free(wuserid);
            return -1;
        }
        
        strcpy(profile_dir, mbstr);

        free(mbstr);
    }

    free(wuserid);

    return ret;
}

/* retrieve OrangeFS credentials from cert */
static unsigned int get_cert_credentials(char *userid,
                                         char *cert_dir_prefix,
                                         char *ca_path,
                                         PVFS_credentials *credentials)
{
    char cert_path[MAX_PATH];
    char *temp;
    X509 *cert, *ca_cert;
    int ret;

    if (userid == NULL || credentials == NULL ||
        ca_path)
        return -1;

    /* checked for cached credentials */
    ret = get_cached_credentials(userid, credentials);
    if (ret == 0)
    {
        /* cache hit */
        return 0;
    }
    else if (ret != 1)
    {
        /* error */
        return ret;
    }

    /* credentials not in cache... */

    /* locate the certificate and CA */
    if (cert_dir_prefix != NULL)
    {
        if ((strlen(cert_dir_prefix) + strlen(userid) + 10) > MAX_PATH)
        {
            DbgPrint("User %s: path to cert too long\n", userid);
            return -1;
        }

        /* cert file is cert.pem in directory of user name */
        strcpy(cert_path, cert_dir_prefix);
        strcat(cert_path, userid);
        strcat(cert_path, "\\cert.pem");
    }
    else
    {
        /* get profile directory */
        ret = get_profile_dir(userid, cert_path);
        if (ret != 0)
        {
            DbgPrint("User %s: could not locate profile dir: %d\n", userid,
                ret);
            return ret;
        }
        
        if (strlen(cert_path) + 9 >= MAX_PATH)
        {
            DbgPrint("User %s: profile dir too long\n", userid);
            return -1;
        }

        strcat(cert_path, "\\cert.pem");
    }

    /* verify the certificate */
    ret = load_cert_from_file(cert_path, &cert);
    if (ret != 0)
        return ret;

    ret = load_cert_from_file(ca_path, &ca_cert);
    if (ret != 0)
    {
        X509_free(cert);
        return ret;
    }
    
    /* read and cache credentials from certificate */

}