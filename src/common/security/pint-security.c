/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "pvfs2.h"
#include "pint-eattr.h"
#include "pvfs2-req-proto.h"
#include "pvfs2-internal.h"
#include "gossip.h"
#include "gen-locks.h"
#include "pint-security.h"


/* TODO: move to global configuration */
#define SECURITY_DEFAULT_KEYSTORE "/tmp/keystore"


static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static int security_init_status = 0;

static gen_mutex_t pubkey_mutex = GEN_MUTEX_INITIALIZER;


static int load_public_keys(char*);


int PINT_security_initialize(void)
{
    gen_mutex_lock(&security_init_mutex);
    if (security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -1;
    }

    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    /* TODO: return value */
    load_public_keys(SECURITY_DEFAULT_KEYSTORE);

    security_init_status = 1;
    gen_mutex_unlock(&security_init_mutex);
 
    return 1;
}

int PINT_security_finalize(void)
{
    gen_mutex_lock(&security_init_mutex);
    if (!security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -1;
    }

    EVP_cleanup();
    ERR_free_strings();

    security_init_status = 0;
    gen_mutex_unlock(&security_init_mutex);
    
    return 1;
}

static int load_public_keys(char *path)
{
    FILE *keyfile;
    int ch, ptr;
    static char buf[1024];
    EVP_PKEY *key;

    keyfile = fopen(path, "r");
    if (keyfile == NULL)
    {
        return -1;
    }

    while (!feof(keyfile))
    {
        do
        {
            ch = fgetc(keyfile);
        } while(isspace(ch));

        if ((ch == EOF) || !isalnum(ch))
        {
            fclose(keyfile);
            return -1;
        }

        for (ptr = 0; (ptr < 1023) && isalnum(ch); ptr++)
        {
            buf[ptr] = (char)ch;
            ch = fgetc(keyfile);
            if (ch == EOF)
            {
                fclose(keyfile);
                return -1;
            }
        }
        buf[ptr] = '\0';

        do
        {
            ch = fgetc(keyfile);
        } while(isspace(ch));

        ungetc(ch, keyfile);

        key = PEM_read_PUBKEY(keyfile, NULL, NULL, NULL);
        if (key == NULL)
        {
            fclose(keyfile);
            return -1;
        }

        /* lookup */

        /* add to hash */
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
