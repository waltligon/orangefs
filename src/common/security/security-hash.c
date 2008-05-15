/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>

#include <openssl/evp.h>

#include "pvfs2-types.h"
#include "quickhash.h"
#include "security-hash.h"
#include "gen-locks.h"

#define DEFAULT_SECURITY_TABLE_SIZE 71

typedef struct pubkey_entry_s {
    struct qlist_head hash_link;
    PVFS_handle host;
    EVP_PKEY *pubkey;
} pubkey_entry_t;

static struct qhash_table *pubkey_table = NULL;
static int hash_init_status = 0;
static gen_mutex_t hash_init_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t hash_pubkey_mutex = GEN_MUTEX_INITIALIZER;

static int pubkey_compare(void*, struct qhash_head*);


int SECURITY_hash_initialize(void)
{
    gen_mutex_lock(&hash_init_mutex);
    if (hash_init_status)
    {
        gen_mutex_unlock(&hash_init_mutex);
        return -1;
    }
    
    pubkey_table = qhash_init(pubkey_compare, quickhash_64bit_hash,
                              DEFAULT_SECURITY_TABLE_SIZE);

    if (pubkey_table == NULL)
    {
    	gen_mutex_unlock(&hash_init_mutex);
        return -1;
    }
    
    hash_init_status = 1;
    gen_mutex_unlock(&hash_init_mutex);

    return 0;
}

void SECURITY_hash_finalize(void)
{
    /* qhash_finalize(pubkey_table); // incomplete free */
    
    gen_mutex_lock(&hash_init_mutex);
    if (!hash_init_status)
    {
        gen_mutex_unlock(&hash_init_mutex);
        return;
    }
    
    qhash_destroy_and_finalize(pubkey_table, pubkey_entry_t, hash_link, free);
    
    hash_init_status = 0;
    gen_mutex_unlock(&hash_init_mutex);
}

int SECURITY_add_pubkey(PVFS_handle host, EVP_PKEY *pubkey)
{
    /* XXX: who is responsible for keys? */
    /* XXX: what about duplicates? */
    
    gen_mutex_lock(&hash_pubkey_mutex);
    pubkey_entry_t *entry;

    entry = (pubkey_entry_t*)malloc(sizeof(pubkey_entry_t));
    if (entry == NULL)
    {
        return -1;
    }

    entry->host = host;
    entry->pubkey = pubkey;
    qhash_add(pubkey_table, &entry->host, &entry->hash_link);
    
    gen_mutex_unlock(&hash_pubkey_mutex);

    return 0;
}

EVP_PKEY *SECURITY_lookup_pubkey(PVFS_handle host)
{
    struct qhash_head *temp;
    pubkey_entry_t *entry;

    temp = qhash_search(pubkey_table, &host);
    if (temp == NULL)
    {
        return NULL;
    }

    entry = qlist_entry(temp, pubkey_entry_t, hash_link);

    return entry->pubkey;
}

static int pubkey_compare(void *key, struct qhash_head *link)
{
    PVFS_handle host = *((PVFS_handle *)key);
    pubkey_entry_t *temp;

    temp = qlist_entry(link, pubkey_entry_t, hash_link);

    return (temp->host == host);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
