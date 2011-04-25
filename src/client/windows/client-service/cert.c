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

    /* add CA cert to trusted store */
    trust_store = X509_STORE_new();
    if (trust_store == NULL)
        return ERR_get_error();

    X509_STORE_add_cert(trust_store, ca_cert);


}