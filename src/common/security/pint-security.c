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
#include "server-config.h"

#include "pint-security.h"
#include "security-hash.h"


/* TODO: move to global configuration */
#define SECURITY_DEFAULT_KEYSTORE "/tmp/keystore"


static gen_mutex_t security_init_mutex = GEN_MUTEX_INITIALIZER;
static int security_init_status = 0;


static int load_public_keys(char*);
static int lookup_host_handle(uint32_t*, const char*);


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
    if (SECURITY_hash_initialize() == -1)
    	return -1;
    
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
    SECURITY_hash_finalize();

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
    uint32_t host;
    int ret;

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

        ret = lookup_host_handle(&host, buf);
        if (ret == -1)
        {
            return -1;
        }

        /* add to hash; return value */
        ret = SECURITY_add_pubkey(host, key);
        if (ret == -1)
        {
            return -1;
        }
        
    }

    fclose(keyfile);

    return 0;
}

static int lookup_host_handle(uint32_t *host, const char *alias)
{
    struct server_configuration_s *config;
    PINT_llist *iter;
    uint32_t index;
    host_alias_s *a;  
    
    config = PINT_get_server_config();
    assert(config);

    index = 0;
    for (iter = config->host_aliases; iter != NULL; iter = iter->next)
    {
        a = (host_alias_s*)iter->item;
        if (a == NULL)
        {
            continue;
        }
        if (strcmp(a->host_alias, alias) == 0)
        {
            *host = index;
            return 0;
        }
        index++;
    }

    return -1;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
