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
#include <stdio.h>
#include <errno.h>

#include <openssl/pem.h>

#include "cert-util.h"

/* load an X509 certificate from a file */
int PINT_load_cert_from_file(const char *path,
                             X509 **cert)
{
    FILE *f;

    if (path == NULL || cert == NULL)
    {
        return -1;
    }

    f = fopen(path, "r");
    if (f == NULL)
    {
        return errno;
    }

    /* TODO: fstat permissions */

    *cert = PEM_read_X509(f, NULL, NULL, NULL);

    fclose(f);

    if (*cert == NULL)
    {
        return -PVFS_ESECURITY;
    }

    return 0;
}

/* load a private key from a file */
int PINT_load_key_from_file(const char *path,
                            EVP_PKEY **key)
{
    FILE *f;

    if (path == NULL || key == NULL)
    {
        return -1;
    }

    f = fopen(path, "r");
    if (f == NULL)
    {
        return errno;
    }

    /* TODO: fstat permissions */

    *key = PEM_read_PrivateKey(f, NULL, NULL, NULL);

    fclose(f);

    if (*key == NULL)
    {
        return -PVFS_ESECURITY;
    }

    return 0;
}

int PINT_cert_to_X509(const PVFS_certificate *cert,
                      X509 **xcert)
{
    BIO *bio_mem;

    if (cert == NULL || xcert == NULL)
    {
        return -1;
    }

    /* create new BIO (basic input/output handle) from memory */
    bio_mem = BIO_new_mem_buf(cert->buf, cert->buf_size);
    if (bio_mem == NULL)
    {
        return -PVFS_ESECURITY;
    }

    /* create certificate struct from memory */
    *xcert = NULL;
    d2i_X509_bio(bio_mem, xcert);

    BIO_free(bio_mem);

    return *xcert ? 0 : -PVFS_ESECURITY;
}

int PINT_X509_to_cert(const X509 *xcert,
                      PVFS_certificate **cert)
{
    BIO *bio_mem;
    size_t cert_size;
    char *p = NULL;

    if (xcert == NULL || cert == NULL)
    {
        return -1;
    }

    /* init memory BIO */
    bio_mem = BIO_new(BIO_s_mem());
    if (bio_mem == NULL)
    {
        return -PVFS_ESECURITY;
    }

    /* write cert to mem BIO */
    if (i2d_X509_bio(bio_mem, (X509 *) xcert) <= 0)
    {
        BIO_free(bio_mem);
        return -PVFS_ESECURITY;
    }

    /* allocate certificate */
    cert_size = BIO_ctrl_pending(bio_mem);
    if (cert_size == 0)
    {
        BIO_free(bio_mem);
        return -PVFS_ESECURITY;
    }
    *cert = (PVFS_certificate *) malloc(sizeof(PVFS_certificate));
    if (*cert == NULL)
    {
        BIO_free(bio_mem);
        return ENOMEM;
    }
    (*cert)->buf = malloc(cert_size);
    if ((*cert)->buf == NULL)
    {
        BIO_free(bio_mem);
        return ENOMEM;
    }
    (*cert)->buf_size = cert_size;

    /* get a pointer to the buffer */
    BIO_get_mem_data(bio_mem, &p);
    if (p == NULL)
    {
        free((*cert)->buf);
        free(*cert);
        BIO_free(bio_mem);
        return -PVFS_ESECURITY;
    }

    /* copy the buffer */
    memcpy((*cert)->buf, p, cert_size);

    BIO_free(bio_mem);

    return 0;
}

int PINT_copy_cert(const PVFS_certificate *src,
                   PVFS_certificate *dest)
{
   if (src == NULL || dest == NULL)
   {
       return -PVFS_EINVAL;
   }

   /* allocate dest buffer and copy */
   if (src->buf != NULL && src->buf_size > 0)
   {
       dest->buf_size = src->buf_size;

       dest->buf = (PVFS_cert_data) malloc(dest->buf_size);
       if (dest->buf == NULL)
       {
           return -PVFS_ENOMEM;
       }
       memcpy(dest->buf, src->buf, dest->buf_size);
   }
   else
   {
       dest->buf = NULL;
       dest->buf_size = 0;
   }

   return 0;
}

void PINT_cleanup_cert(PVFS_certificate *cert)
{
    if (cert)        
    {
        if (cert->buf)
        {   
            free(cert->buf);
            cert->buf = NULL;
        }
        cert->buf_size = 0;
    }
}

