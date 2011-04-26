/* Copyright (C) 2011 Omnibond LLC
   Certificate functions */

#include <Windows.h>
#include <stdio.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

/* initialize OpenSSL */
static void openssl_init()
{
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
static unsigned long load_cert_from_file(char *path, X509 **cert)
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
static unsigned long verify_cert(X509 *cert, X509 *ca_cert)
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
