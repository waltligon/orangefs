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
#include "pvfs2-types.h"
#include "pint-eattr.h"
#include "pvfs2-req-proto.h"
#include "pvfs2-internal.h"
#include "gossip.h"
#include "gen-locks.h"
#include "server-config.h"

#include "pint-security.h"
#include "security-hash.h"


/* TODO: move to global configuration */
#define SECURITY_DEFAULT_KEYSTORE "/tmp/keystore"


static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static int security_init_status = 0;


static int load_public_keys(char*);


/*  PINT_security_initialize	
 *
 *  Initializes the security module
 *	
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_EIO if key file is missing or invalid
 *  returns 0 on sucess
 */
int PINT_security_initialize(void)
{
    int ret;

    gen_mutex_lock(&security_init_mutex);
    if (security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();

    ret = SECURITY_hash_initialize();
    if (ret < 0)
    {
        return ret;
    }
    
    /* TODO: better error handling */
    ret = load_public_keys(SECURITY_DEFAULT_KEYSTORE);
    if (ret < 0)
    {
        return -PVFS_EIO;
    }

    security_init_status = 1;
    gen_mutex_unlock(&security_init_mutex);
 
    return 0;
}

/*  PINT_security_finalize	
 *
 *  Finalizes the security module
 *	
 *  returns PVFS_EALREADY if already finalized
 *  returns 0 on sucess
 */
int PINT_security_finalize(void)
{
    gen_mutex_lock(&security_init_mutex);
    if (!security_init_status)
    {
        gen_mutex_unlock(&security_init_mutex);
        return -PVFS_EALREADY;
    }

    EVP_cleanup();
    ERR_free_strings();
    SECURITY_hash_finalize();

    security_init_status = 0;
    gen_mutex_unlock(&security_init_mutex);
    
    return 0;
}

/*  load_public_keys
 *
 *  Internal function to load keys from a file.
 *  File path includes the filename
 *  When finished without error, hash table will be filled
 *  with all host ID / public key pairs.
 *	
 *  returns -1 on file I/O error
 *  returns -2 on host lookup failure
 *  returns -3 on hash table failure
 *  returns 0 on sucess
 */
static int load_public_keys(char *path)
{
    FILE *keyfile;
    int ch, ptr;
    static char buf[1024];
    EVP_PKEY *key;
    char *host;
    int ret;

    keyfile = fopen(path, "r");
    if (keyfile == NULL)
    {
        return -1;
    }

    ch = fgetc(keyfile);

    while (ch != EOF)
    {
        while (isspace(ch))
        {
            ch = fgetc(keyfile);
        }

        if (ch == EOF)
        {
            break;
        }

        if (!isalnum(ch))
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

        host = PINT_config_get_host_addr_ptr(PINT_get_server_config(), buf);
        if (host == NULL)
        {
            fclose(keyfile);
            return -2;
        }

        ret = SECURITY_add_pubkey(host, key);
        if (ret < 0)
        {
            fclose(keyfile);
            return -3;
        }

        ch = fgetc(keyfile);
        
    }

    fclose(keyfile);

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
