/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "pvfs2-types.h"
#include "quickhash.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "security-hash.h"
#include "gen-locks.h"


/*  71 seems to be a reasonable size, could be increased
 *  if collisions start to become a problem. Prime numbers
 *  are preferred. */
#define DEFAULT_SECURITY_TABLE_SIZE 71


/*  TODO: Serialize hash table at some point if wanted */


typedef struct pubkey_entry_s {
    struct qlist_head hash_link;    /* holds prev/next pointers */
    char *host;                     /* BMI host address */
    EVP_PKEY *pubkey;               /* public key for above host ID */
} pubkey_entry_t;


static struct qhash_table *pubkey_table = NULL;   /* head of the table */
static int hash_init_status = 0;                  /* 1 = init, 0 = not init */
static gen_mutex_t hash_mutex = GEN_MUTEX_INITIALIZER;


static int pubkey_compare(void*, struct qhash_head*);
static void free_pubkey_entry(void*);


/*  SECURITY_hash_initialize
 *
 *  Initializes the hash table for use
 *
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_ENOMEM if memory cannot be allocated
 *  returns 0 on success
 */
int SECURITY_hash_initialize(void)
{
    gen_mutex_lock(&hash_mutex);
    if (hash_init_status)
    {
        gen_mutex_unlock(&hash_mutex);
        return -PVFS_EALREADY;
    }
    
    pubkey_table = qhash_init(pubkey_compare, quickhash_string_hash,
                              DEFAULT_SECURITY_TABLE_SIZE);

    if (pubkey_table == NULL)
    {
    	gen_mutex_unlock(&hash_mutex);
        return -PVFS_ENOMEM;
    }
    
    hash_init_status = 1;
    gen_mutex_unlock(&hash_mutex);
    return 0;
}

/*  SECURITY_hash_finalize
 *
 *  Frees everything allocated within the table
 *  and anything used to set it up
 *
 *  returns nothing
 */
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

/*  SECURITY_add_pubkey
 *
 *  Takes an EVP_PKEY and inserts it into the hash table
 *  based on the BMI host address.  If the host address already
 *  exists in the table, it's corresponding key is replaced 
 *  with the new one
 *
 *  returns PVFS_ENOMEM if memory cannot be allocated
 *  returns 0 on success
 */
int SECURITY_add_pubkey(char *host, EVP_PKEY *pubkey)
{    
    pubkey_entry_t *entry;
    struct qhash_head *temp;

    entry = (pubkey_entry_t *)malloc(sizeof(pubkey_entry_t));
    if (entry == NULL)
    {
        return -PVFS_ENOMEM;
    }

    entry->host = strdup(host);
    if (entry->host == NULL)
    {
        return -PVFS_ENOMEM;
    }
    entry->pubkey = pubkey;
    
    gen_mutex_lock(&hash_mutex);
    
    // remove prior key linked to the host address if it exists
    temp = qhash_search_and_remove(pubkey_table, host);
    if (temp != NULL) 
    {
    	gossip_debug(GOSSIP_SECURITY_DEBUG, "Removed duplicate key from table.\n");
    	free_pubkey_entry(temp);
    }
    qhash_add(pubkey_table, &entry->host, &entry->hash_link);
    
    gen_mutex_unlock(&hash_mutex);
    return 0;
}

/*  SECURITY_lookup_pubkey
 *
 *  Takes a host address and returns a pointer to the
 *  matching EVP_PKEY structure
 *
 *  returns NULL if no matching key is found
 */
EVP_PKEY *SECURITY_lookup_pubkey(char *host)
{
    struct qhash_head *temp;
    pubkey_entry_t *entry;

    temp = qhash_search(pubkey_table, host);
    if (temp == NULL)
    {
    	return NULL;
    }

    entry = qlist_entry(temp, pubkey_entry_t, hash_link);

    return entry->pubkey;
}

/*  pubkey_compare
 *
 *  Takes in a key (in this case a host address) and compares
 *  it to the value of the host address contained within the
 *  structure passed in
 *
 *  returns 1 if the IDs match
 *  returns 0 if they do not match or if the structure is invalid
 */
static int pubkey_compare(void *key, struct qhash_head *link)
{
    char *host = ((char *)key);
    pubkey_entry_t *temp;

    temp = qlist_entry(link, pubkey_entry_t, hash_link);
    
    if (temp == NULL) return 0;

    return (!strcmp(temp->host, host));
}

/*  free_pubkey_entry
 *
 *  Takes in a pointer to a pubkey_entry_t structure to free
 *  Frees the key structure within as well as the passed-in struct
 *
 *  no return value
 */
static void free_pubkey_entry(void *to_free) 
{
    pubkey_entry_t *temp = (pubkey_entry_t *)to_free;
    if (temp != NULL)
    {
        free(temp->pubkey);
        free(temp->host);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
 
