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


/*  TODO: Serialize hash table at some point if wanted */


typedef struct pubkey_entry_s {
    struct qlist_head hash_link;
    uint32_t host;
    EVP_PKEY *pubkey;
} pubkey_entry_t;


static struct qhash_table *pubkey_table = NULL;
static int hash_init_status = 0;
static gen_mutex_t hash_mutex = GEN_MUTEX_INITIALIZER;


static int pubkey_compare(void*, struct qhash_head*);
static void free_pubkey_entry(void*);


int SECURITY_hash_initialize(void)
{
    gen_mutex_lock(&hash_mutex);
    if (hash_init_status)
    {
        gen_mutex_unlock(&hash_mutex);
        return -1;
    }
    
    pubkey_table = qhash_init(pubkey_compare, quickhash_32bit_hash,
                              DEFAULT_SECURITY_TABLE_SIZE);

    if (pubkey_table == NULL)
    {
    	gen_mutex_unlock(&hash_mutex);
        return -1;
    }
    
    hash_init_status = 1;
    gen_mutex_unlock(&hash_mutex);

    return 0;
}

void SECURITY_hash_finalize(void)
{
    gen_mutex_lock(&hash_mutex);
    if (!hash_init_status)
    {
        gen_mutex_unlock(&hash_mutex);
        return;
    }
    
    qhash_destroy_and_finalize(pubkey_table, pubkey_entry_t, hash_link, 
                               free_pubkey_entry);
    
    hash_init_status = 0;
    gen_mutex_unlock(&hash_mutex);
}

int SECURITY_add_pubkey(uint32_t host, EVP_PKEY *pubkey)
{    
    gen_mutex_lock(&hash_mutex);
    pubkey_entry_t *entry;
    struct qhash_head *temp;

    entry = (pubkey_entry_t *)malloc(sizeof(pubkey_entry_t));
    if (entry == NULL)
    {
        return -1;
    }

    entry->host = host;
    entry->pubkey = pubkey;
    
    temp = qhash_search_and_remove(pubkey_table, &host);
    if (temp != NULL) 
    {
    	free_pubkey_entry(temp);
    }
    qhash_add(pubkey_table, &entry->host, &entry->hash_link);
    
    gen_mutex_unlock(&hash_mutex);

    return 0;
}

EVP_PKEY *SECURITY_lookup_pubkey(uint32_t host)
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
    uint32_t host = *((uint32_t *)key);
    pubkey_entry_t *temp;

    temp = qlist_entry(link, pubkey_entry_t, hash_link);

    return (temp->host == host);
}

static void free_pubkey_entry(void *to_free) 
{
	pubkey_entry_t *temp = (pubkey_entry_t *)to_free;
	if (temp != NULL)
	{
		free(temp->pubkey);
		free(temp);
	}
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
