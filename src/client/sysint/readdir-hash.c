/* 
 * (C) 2016 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <string.h>

#include "pvfs2-types.h"
#include "quickhash.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "readdir-hash.h"
#include "gen-locks.h"
#include "assert.h"


/*  1009 seems to be a reasonable size, could be increased
 *  if collisions start to become a problem. Prime numbers
 *  are preferred. */
#define DEFAULT_READDIR_TABLE_SIZE 1009

/* data to be stored in an entry */
typedef struct readdir_payload
{
    PVFS_object_ref ref;
    PVFS_ds_position token;
    int32_t dirdata_index;
} readdir_payload_t;

typedef struct readdir_key
{
    PVFS_object_ref ref;
    PVFS_ds_position token;
} readdir_key_t;

typedef struct readdir_entry_s {
    struct qlist_head hash_link;  /* holds prev/next pointers */
    int hash_key;                 /* hash key */
    readdir_payload_t *readdir_payload; /* public key for above hash_key */
} readdir_entry_t;


static struct qhash_table *readdir_table = NULL;   /* head of the table */
static int hash_init_status = 0;                  /* 1 = init, 0 = not init */
static gen_mutex_t hash_mutex = GEN_MUTEX_INITIALIZER;


static int readdir_hash_key(const void* key, int table_size);
static int readdir_compare(const void*, struct qhash_head*);
static void free_readdir_entry(void*);


/*  readdir_hash_initialize
 *
 *  Initializes the hash table for use
 *
 *  returns PVFS_EALREADY if already initialized
 *  returns PVFS_ENOMEM if memory cannot be allocated
 *  returns 0 on success
 */
int readdir_hash_initialize(void)
{
    gen_mutex_lock(&hash_mutex);
    if (hash_init_status)
    {
        gen_mutex_unlock(&hash_mutex);
        return -PVFS_EALREADY;
    }
    
    readdir_table = qhash_init(readdir_compare, readdir_hash_key,
                              DEFAULT_READDIR_TABLE_SIZE);

    if (readdir_table == NULL)
    {
    	gen_mutex_unlock(&hash_mutex);
        return -PVFS_ENOMEM;
    }
    
    hash_init_status = 1;
    gen_mutex_unlock(&hash_mutex);
    return 0;
}

/*  readdir_hash_finalize
 *
 *  Frees everything allocated within the table
 *  and anything used to set it up
 */
void readdir_hash_finalize(void)
{
    gen_mutex_lock(&hash_mutex);
    if (!hash_init_status)
    {
        gen_mutex_unlock(&hash_mutex);
        return;
    }
    
    qhash_destroy_and_finalize(readdir_table, readdir_entry_t, hash_link, 
                               free_readdir_entry);
    
    hash_init_status = 0;
    gen_mutex_unlock(&hash_mutex);
}

/*  readdir_add_token
 *
 *  Takes an readdir token and inserts it into the hash table
 *  based on the hash key.  If the hash key already
 *  exists in the table, it's corresponding dirdata_index is replaced 
 *  with the new one.
 *
 *  The token will be freed upon removal from the table.
 *
 *  returns PVFS_ENOMEM if memory cannot be allocated
 *  returns 0 on success
 */
int readdir_add_token(int hash_key, PVFS_object_ref ref,
                      PVFS_ds_position token, int32_t dirdata_index)
{    
    readdir_entry_t *entry;
    struct qhash_head *temp;

    entry = (readdir_entry_t *)malloc(sizeof(readdir_entry_t));
    if (entry == NULL)
    {
        return -PVFS_ENOMEM;
    }

    entry->readdir_payload = (readdir_payload_t *)malloc(sizeof(readdir_payload_t));
    if (entry->readdir_payload == NULL)
    {
        free(entry);
        return -PVFS_ENOMEM;
    }

    entry->readdir_payload->ref = ref;
    entry->readdir_payload->token = token;
    entry->readdir_payload->dirdata_index = dirdata_index;
    
    gen_mutex_lock(&hash_mutex);
    
    /* remove prior key linked to the hash key if it exists */
    temp = qhash_search_and_remove(readdir_table, &hash_key);
    if (temp != NULL) 
    {
    	gossip_debug(GOSSIP_READDIR_DEBUG, 
    	             "Removed duplicate key from table.\n");
    	free_readdir_entry(temp);
    }
    qhash_add(readdir_table, &entry->hash_key, &entry->hash_link);
    
    gen_mutex_unlock(&hash_mutex);
    return 0;
}

/*  readdir_lookup_token
 *
 *  Takes a readdir token and returns the matching dirdata index
 *
 *  returns NULL if no matching key is found
 */
int32_t readdir_lookup_token(int hash_key)
{
    struct qhash_head *temp;
    readdir_entry_t *entry;

    temp = qhash_search(readdir_table, &hash_key);
    if (temp == NULL)
    {
    	return -1;
    }

    entry = qlist_entry(temp, readdir_entry_t, hash_link);

    return entry->readdir_payload->dirdata_index;
}

/* hash from http://burtleburtle.net/bob/hash/evahash.html */
#define mix(a,b,c) \
do { \
  a=a-b;  a=a-c;  a=a^(c>>13); \
      b=b-c;  b=b-a;  b=b^(a<<8);  \
      c=c-a;  c=c-b;  c=c^(b>>13); \
      a=a-b;  a=a-c;  a=a^(c>>12); \
      b=b-c;  b=b-a;  b=b^(a<<16); \
      c=c-a;  c=c-b;  c=c^(b>>5);  \
      a=a-b;  a=a-c;  a=a^(c>>3);  \
      b=b-c;  b=b-a;  b=b^(a<<10); \
      c=c-a;  c=c-b;  c=c^(b>>15); \
} while(0)


/* readdir_hash_key()
 *
 * hash function for character pointers
 *
 * returns hash index
 */
static int readdir_hash_key(const void* key, int table_size)
{
    const readdir_key_t * key_entry = (const readdir_key_t *) key;

    uint32_t a = (uint32_t)(key_entry->ref.handle >> 32);
    uint32_t b = (uint32_t)(key_entry->ref.handle & 0x00000000FFFFFFFF);
    uint32_t c = (uint32_t)(key_entry->token);

    mix(a,b,c);
    return (int)(c & (DEFAULT_READDIR_TABLE_SIZE-1));
}

/* readdir_compare()
 *
 * compares an opaque key (char* in this case) against a payload to see
 * if there is a match
 *
 * returns 1 on match, 0 otherwise
 */
static int readdir_compare(const void* key, struct qhash_head* link)
{
    const readdir_key_t * real_key = (const readdir_key_t *)key;
    readdir_payload_t * tmp_payload = NULL;
    readdir_entry_t * tmp_entry = NULL;

    tmp_entry = qhash_entry(link, readdir_entry_t, hash_link);
    assert(tmp_entry);

    tmp_payload = (readdir_payload_t *)tmp_entry->readdir_payload;
     /* If the following aren't equal, we know we don't have a match
      *   - ref.handle
      *   - ref.fs_id
      *   - token
      */
    if( real_key->ref.handle  != tmp_payload->ref.handle ||
        real_key->ref.fs_id   != tmp_payload->ref.fs_id  ||
        real_key->token != tmp_payload->token )
    {
        /* One of the above cases failed, so we know these aren't a match */
        return(0);
    }

    return(1);
}


/*  free_readdir_entry
 *
 *  Takes in a pointer to a readdir_entry_t structure to free
 *  Frees the key structure within as well as the passed-in struct
 *
 *  no return value
 */
static void free_readdir_entry(void *to_free) 
{
    readdir_entry_t *temp = (readdir_entry_t *)to_free;
    if (temp != NULL)
    {
        free(temp->readdir_payload);
        free(temp);
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
 
