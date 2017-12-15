/*                                                              
 * Copyright (C) 2012 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * Certificate functions
 *                                                               
*/

#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pint-security.h"

#include "pint-cert.h"

X509_STORE *trust_store;

/* initialize the global trust store */
int PINT_init_trust_store()
{

    trust_store = X509_STORE_new();

    if (trust_store == NULL)
    {
        return -PVFS_ESECURITY;
    }

    return 0;
}

/* add a certificate to the trust store
   (usually the CA certificate */
int PINT_add_trusted_certificate(X509 *cert)
{
    int ret;

    ret = X509_STORE_add_cert(trust_store, cert);

    if (!ret)
    {
        return -PVFS_ESECURITY;
    }

    return 0;
}

/* free the global trust store */
void PINT_cleanup_trust_store()
{
    if (trust_store != NULL)
    {
        X509_STORE_free(trust_store);
    }
    trust_store = NULL;

}

/* callback to output errors to gossip */
static int verify_certificate_cb(int ok, X509_STORE_CTX *ctx)
{
    int err;
    X509 *cert;
    X509_NAME *subject;
    char buf[256] = "(unknown)";

    if (!ok)
    {
        /* get cert subject */
        cert = X509_STORE_CTX_get_current_cert(ctx);
        if (cert != NULL)
        {
            subject = X509_get_subject_name(cert);
            if (subject != NULL) 
            {
                X509_NAME_oneline(subject, buf, sizeof(buf));
                buf[255] = '\0';
            }
        }
        gossip_err("Certificate %s has a verification error:\n", buf);
        err = X509_STORE_CTX_get_error(ctx);
        gossip_err("Error %d at depth %d: %s\n", err,
                   X509_STORE_CTX_get_error_depth(ctx), 
                   X509_verify_cert_error_string(err));
    }

    return ok;
}

/* verify the certificate and see if it is trusted */
int PINT_verify_certificate(X509 *cert)
{
    X509_STORE_CTX *ctx;
    int ret;

    if (cert == NULL)
    {
        return -1;
    }

    /* create and set up the context */
    ctx = X509_STORE_CTX_new();
    if (ctx == NULL)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);

        return -PVFS_ESECURITY;
    }

    /* set our callback for logging errors */
#if OPENSSL_VERSION_NUMBER & 0x10000000
    X509_STORE_set_verify_cb(trust_store, verify_certificate_cb);
#else
    X509_STORE_set_verify_cb_func(trust_store, verify_certificate_cb);
#endif

    ret = X509_STORE_CTX_init(ctx, trust_store, cert, NULL);
    if (ret == 0)
    {
        PINT_security_error(__func__, -PVFS_ESECURITY);

        X509_STORE_CTX_cleanup(ctx);
        X509_STORE_CTX_free(ctx);

        return -PVFS_ESECURITY;
    }

    /* verify that the cert is trusted by the CA cert */
    ret = (X509_verify_cert(ctx) > 0) ? 0 : -PVFS_ESECURITY;

    /* wrapup */
    X509_STORE_CTX_cleanup(ctx);
    X509_STORE_CTX_free(ctx);

    return ret;
}

