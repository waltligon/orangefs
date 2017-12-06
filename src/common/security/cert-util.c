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

#include "pvfs2-internal.h"
#include "cert-util.h"

#define KEY_TYPE_PUBLIC  0
#define KEY_TYPE_PRIVATE 1

/* load an X509 certificate from a file */
int PINT_load_cert_from_file(const char *path,
                             X509 **cert)
{
    FILE *f;

    if (path == NULL || cert == NULL)
    {
        return -PVFS_EINVAL;
    }

    f = fopen(path, "r");
    if (f == NULL)
    {
        return errno;
    }

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
        return -PVFS_EINVAL;
    }

    f = fopen(path, "r");
    if (f == NULL)
    {
        return errno;
    }

    *key = PEM_read_PrivateKey(f, NULL, NULL, NULL);

    fclose(f);

    if (*key == NULL)
    {
        return -PVFS_ESECURITY;
    }

    return 0;
}

int PINT_save_cert_to_file(const char *path,
                           X509 *cert)
{
    FILE *f;
    int ret;

    if (path == NULL || cert == NULL)
    {
        return -PVFS_EINVAL;
    }

    f = fopen(path, "w");
    if (f == NULL)
    {
        return errno;
    }

    ret = PEM_write_X509(f, cert);

    fclose(f);

    return ret ? 0 : -PVFS_ESECURITY;
}

/* save a key struct to disk */
static int PINT_save_key_to_file(const char *path,
                                 EVP_PKEY *key,
                                 int key_type)
{
    FILE *f;
    int ret;

    if (path == NULL || key == NULL)
    {
        return -PVFS_EINVAL;
    }

    f = fopen(path, "w");
    if (f == NULL)
    {
        return errno;
    }

    if (key_type == KEY_TYPE_PUBLIC)
    {
        ret = PEM_write_PUBKEY(f, key);
    }
    else if (key_type == KEY_TYPE_PRIVATE)
    {
        ret = PEM_write_PrivateKey(f, key, NULL, NULL, 0, NULL, NULL);
    }
    else
    {
        fclose(f);
        return -PVFS_EINVAL;
    }

    fclose(f);

    return ret ? 0 : -PVFS_ESECURITY;
}

int PINT_save_pubkey_to_file(const char *path,
                             EVP_PKEY *key)
{
    return PINT_save_key_to_file(path, key, KEY_TYPE_PUBLIC);
}

int PINT_save_privkey_to_file(const char *path,
                              EVP_PKEY *key)
{
    return PINT_save_key_to_file(path, key, KEY_TYPE_PRIVATE);
}

int PINT_cert_to_X509(const PVFS_certificate *cert,
                      X509 **xcert)
{
    BIO *bio_mem;

    if (cert == NULL || xcert == NULL)
    {
        return -PVFS_EINVAL;
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
        return -PVFS_EINVAL;
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
    (*cert)->buf = (PVFS_cert_data) malloc(cert_size);
    if ((*cert)->buf == NULL)
    {
        free(*cert);
        *cert = NULL;
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
        *cert = NULL;
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

int PINT_copy_key(const PVFS_security_key *src,
                  PVFS_security_key *dest)
{
    if (src == NULL || dest == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* allocate dest buffer and copy */
    if (src->buf != NULL && src->buf_size > 0)
    {
        dest->buf_size = src->buf_size;

        dest->buf = (PVFS_key_data) malloc(dest->buf_size);
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

void PINT_cleanup_key(PVFS_security_key *key)
{
    if (key)
    {
        if (key->buf)
        {
            free(key->buf);
            key->buf = NULL;
        }
        key->buf_size = 0;
    }
}
