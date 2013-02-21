/*
 * (C) 2013 Clemson University
 *
 * See COPYING in top-level directory.
 */
#include <pvfs2-config.h>
#ifdef OFS_CAPCACHE_ENABLE
#include <malloc.h>
#include <string.h>
#include <assert.h>
#include "capcache.h"
#include "security-util.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include <murmur3.h>
#include <gossip.h>
#include <pvfs2-debug.h>

/* Global Variables */
struct capcache_s * capcache = NULL;
capcache_lock_t lock;

/* Sizes of Structures/Types */
uint64_t capcache_s_size = sizeof(struct capcache_s);
uint64_t capcache_stats_s_size = sizeof(struct capcache_stats_s);
uint64_t capcache_entry_s_size = sizeof(struct capcache_entry_s);
uint64_t capcache_lock_t_size = sizeof(capcache_lock_t);

/* Internal Only Capability Cache Functions */
void PINT_capcache_debug_capability(const PVFS_capability *cap,
    const char *prefix);
int PINT_capcache_entry_expired(void *e1, void *e2);
void PINT_capcache_rm_expired_entries(PVFS_boolean all, uint16_t index );
static uint16_t PINT_capcache_get_index(const PVFS_capability *cap);
static int PINT_capcache_capability_cmp (
    void * cap1,
    void * cap2);
static void PINT_capcache_reset_stats(struct capcache_stats_s const *stats);
static int PINT_capcache_convert_capability(
    const PVFS_capability * cap,
    struct capcache_entry_s * entry);
void PINT_capcache_cleanup_entry(void *);
static int lock_init(capcache_lock_t * lock);
static inline int lock_lock(capcache_lock_t * lock);
static inline int lock_unlock(capcache_lock_t * lock);
static inline int lock_trylock(capcache_lock_t * lock);
/* END of Internal Only Capability Cache Functions */

/* PINT_capcache_debug_capability
 *
 * Outputs the fields of a capability.
 * prefix should typically be "Caching" or "Removing".
 */
void PINT_capcache_debug_capability(
    const PVFS_capability *cap,
    const char *prefix)
{
    char sig_buf[10], mask_buf[10];
    int i;

    assert(cap);

    if (strlen(cap->issuer) == 0)
    {
        gossip_debug(GOSSIP_CAPCACHE_DEBUG, "%s null capability\n", prefix);
        return;
    }

    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "%s capability:\n", prefix);
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tissuer: %s\n", cap->issuer);
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tfsid: %u\n", cap->fsid);
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tsig_size: %u\n", cap->sig_size);
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tsignature: %s\n",
                 PINT_util_bytes2str(cap->signature, sig_buf, 4));
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\ttimeout: %d\n",
                 (int) cap->timeout);
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\top_mask: %s\n",
                 PINT_print_op_mask(cap->op_mask, mask_buf));
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tnum_handles: %u\n",
                 cap->num_handles);
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tfirst handle: %llu\n",
                 cap->num_handles > 0 ? llu(cap->handle_array[0]) : 0LL);
    for (i = 1; i < cap->num_handles; i++)
    {
        gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\thandle %d: %llu\n",
                     i+1, llu(cap->handle_array[i]));
    }
}

/** Initializes the Capability Cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_init(void)
{
    PFI
    /* Initialize Capability Cache Lock */
    lock_init(&lock);
    /* Acquire the lock */
    lock_lock(&lock);

    /* Acquire Memory */
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "malloc capcache_s_size = %llu\n",
        (long long unsigned int) capcache_s_size);
    capcache = (capcache_t *) malloc(capcache_s_size);
    if ((void *) capcache == NULL)
    {
        /* Unlock the Capability Cache Lock */
        lock_unlock(&lock);
        return -PVFS_ENOMEM;
    }
    /* Clear the ucache_s structure */
    memset(capcache, 0, capcache_s_size);
    /* Cache Structure allocation successful */
    capcache->size_limit = CAPCACHE_SIZE_CAP;
    capcache->default_timeout_length = CAPCACHE_TIMEOUT;
    capcache->entry_limit = CAPCACHE_ENTRY_LIMIT;
    gossip_debug(GOSSIP_CAPCACHE_DEBUG,
        "malloc capcache_stats_s_size = %llu\n",
        (long long unsigned int) capcache_stats_s_size);
    capcache->stats = (struct capcache_stats_s *) malloc(
        capcache_stats_s_size);
    if(!capcache->stats)
    {
        goto destroy_capcache;
    }
    PINT_capcache_reset_stats(capcache->stats);
    capcache->stats->cache_size = capcache_s_size;

    gossip_debug(GOSSIP_CAPCACHE_DEBUG,
        "initializing the heads of hash table chains\n");
    int i = 0;
    for(; i < CAPCACHE_HASH_MAX; i++)
    {
        /* note a malloc is performed, don't forget to free up mem */
        capcache->entries[i] = PINT_llist_new();
        if(!capcache->entries[i])
        {
            /* Recover */
            goto destroy_head_entries;
        }
    }

    /* Allocate space for hash chain head entries */
    int hash_index = 0;
    for(; hash_index < CAPCACHE_HASH_MAX; hash_index++)
    {
        struct capcache_entry_s * entry_p = NULL;
        gossip_debug(GOSSIP_CAPCACHE_DEBUG,
            "malloc capcache_entry_s_size = %llu\n",
            (long long unsigned int) capcache_entry_s_size);
        entry_p = (struct capcache_entry_s *) malloc(capcache_entry_s_size);
        if(!capcache->entries[hash_index])
        {
            goto destroy_head_entries;
        }
        memset(entry_p, 0, capcache_entry_s_size);
        entry_p->expiration = 0xFFFFFFFF;
        PINT_llist_add_to_head(capcache->entries[hash_index],
            (void *) entry_p);
    }
    /* Unlock the Capability Cache Lock */
    lock_unlock(&lock);
    return 0;

destroy_head_entries:
    hash_index = 0;
    for(; hash_index < CAPCACHE_HASH_MAX; hash_index++)
    {
        PINT_llist_free(capcache->entries[hash_index],
            &PINT_capcache_cleanup_entry);
    }
    if(capcache->stats)
    {
        free(capcache->stats);
    }
destroy_capcache:
    if(capcache)
    {
        free(capcache);
    }
    /* Unlock the Capability Cache Lock */
    lock_unlock(&lock);
    return -PVFS_ENOMEM;
}


/** Releases resources used by the Capability Cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_finalize(void)
{
    PFI
    int rc = 0;
    rc = lock_lock(&lock);
    if(rc != 0)
    {
        return -PVFS_ENOLCK;
    }

    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "Finalizing Capability Cache...\n");

    if(capcache)
    {
        if(capcache->stats)
        {
            free(capcache->stats);
        }
        /* Loop over capcache entries, freeing entry payload and entry */
        int hash_index = 0;
        for(; hash_index < CAPCACHE_HASH_MAX; hash_index++)
        {
            gossip_debug(GOSSIP_CAPCACHE_DEBUG,
                "Freeing entries in hash table chain at index = %d\n",
                hash_index);
            PINT_llist_free(capcache->entries[hash_index],
                &PINT_capcache_cleanup_entry);
        }
        free (capcache);
        capcache = NULL;
        gossip_debug(GOSSIP_CAPCACHE_DEBUG, "Capability Cache freed...\n");
    }
    rc = lock_unlock(&lock);
    if(rc != 0)
    {
        return -PVFS_ENOLCK;
    }
    return 0;
}

/** PINT_capcache_entry_expired
 * Compares capcache entry's timeout to that of a 'dummy' entry with the
 * timeout set to the current time.
 * Returns 0 if entry 'e2' has expired; otherwise, returns 1.
 */
int PINT_capcache_entry_expired(void *e1, void *e2)
{
    //PFI

    /* TODO: remove these later */
    assert(e1);
    assert(e2);

    if(((struct capcache_entry_s *) e1)->expiration >=
        ((struct capcache_entry_s *) e2)->expiration)
    {
        /* Entry has expired */
        return 0;
    }
    return 1;
}

/** PINT_capcache_rm_expired_entries
 * If 'all' is non-zero, then all hash table chains are scanned for expired
 * entries.
 *
 * If 'all' is zero, then only the hash table chain at index is scanned for expired
 * entries.
 *
 * In both cases, an expired entry's capability is cleaned up, the entry
 * freed, and removed from the linked list.
 */
void PINT_capcache_rm_expired_entries(PVFS_boolean all, uint16_t index)
{
    PFI
    lock_lock(&lock);
    struct capcache_entry_s now;
    now.expiration = time(NULL);
    /* Removes expired entries from all hash table chains */
    if(all)
    {
        gossip_debug(GOSSIP_CAPCACHE_DEBUG,
            "Removing all entries with timeouts before %llu\n",
            (long long unsigned int) now.expiration);
        int hash_index = 0;
        for(; hash_index < CAPCACHE_HASH_MAX; hash_index++)
        {
            gossip_debug(GOSSIP_CAPCACHE_DEBUG,
                "searching chain at hash_index = %d\n",
                hash_index);
            struct capcache_entry_s * entry_rm = NULL;
            while((entry_rm = PINT_llist_rem(
                capcache->entries[hash_index],
                &now,
                &PINT_capcache_entry_expired)) != NULL)
            {
                PINT_capcache_debug_capability(entry_rm->cap,
                    "***** REMOVING *****");
                PINT_capcache_cleanup_entry(entry_rm);
            }
        }
    }
    else
    {
        /* Remove expired entries from the hash table chain at index */
        gossip_debug(GOSSIP_CAPCACHE_DEBUG,
            "searching chain at hash_index = %d\n", index);
        struct capcache_entry_s * entry_rm = NULL;
        while((entry_rm = PINT_llist_rem(
                capcache->entries[index],
                &now,
                &PINT_capcache_entry_expired)) != NULL)
        {
            PINT_capcache_debug_capability(entry_rm->cap,
                "***** REMOVING *****");
            PINT_cleanup_capability(entry_rm->cap);
            free(entry_rm);
        }
    }
    lock_unlock(&lock);
}

/** Lookup
 * Returns pointer to Capability Cache Entry on success.
 * Returns NULL on failure.
 */
struct capcache_entry_s * PINT_capcache_lookup_entry(PVFS_capability * cap)
{
    PFI

    struct capcache_entry_s * current = NULL;
    uint16_t index = 0;

    if(!capcache || !cap)
    {
        return NULL;
    }

    /* Get index of the hash table chain to look in */
    index = PINT_capcache_get_index(cap);

    /* Acquire the lock */
    lock_lock(&lock);

    /* Iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    current = (struct capcache_entry_s *) PINT_llist_search(
        capcache->entries[index],
        cap,
        &PINT_capcache_capability_cmp);
    /* Unlock the Capability Cache Lock */
    lock_unlock(&lock);
    return current;
}

/** PINT_capcache_insert_entry
 * Inserts 'cap' into the Capability Cache.
 * Returns 0 on succes; otherwise returns -PVFS_error.
 */
int PINT_capcache_insert_entry(const PVFS_capability * cap)
{
    PFI

    int rc = 0;
    uint16_t index = 0;
    struct capcache_entry_s * entry = NULL;

    if(!capcache || !cap)
    {
        return -PVFS_EINVAL;
    }

    /* Determine which hash table chain we'll be working with */
    index = PINT_capcache_get_index(cap);

    /* Remove Expired Capabilities in this chain */
    PINT_capcache_rm_expired_entries(0, index);

    entry = (struct capcache_entry_s *) malloc(sizeof(struct capcache_entry_s));
    if(!entry)
    {
        return -PVFS_ENOMEM;
    }

    rc = PINT_capcache_convert_capability(cap, entry);
    if(rc != 0)
    {
        rc = -PVFS_ENOMEM;
        goto destroy_entry;
    }

    PINT_capcache_debug_capability(entry->cap, "Caching");

    /* Acquire the lock */
    lock_lock(&lock);
    gossip_debug(GOSSIP_CAPCACHE_DEBUG,
        "entry added to the head of the linked list @ index = %d\n", index);
    if(PINT_llist_add_to_head(capcache->entries[index], entry) < 0)
    {
        rc = -PVFS_ENOMEM;
        goto destroy_entry;
    }
    /* Unlock the Capability Cache Lock */
    lock_unlock(&lock);
    return 0;

destroy_entry:
    free(entry);
    return rc;
}

/* Remove */
int PINT_capcache_remove_entry(void)
{
    PFI
    int rc = 0;
    lock_lock(&lock);
    /*
    uint16_t index = 0;
    index = PINT_capcache_get_index(cap);
    struct capcache_entry_s * = PINT_llist_rem(
        capcache->entries[index],
        cap,
        &PINT_capcache_capability_cmp);
    */
    lock_unlock(&lock);
    return rc;
}

/*****************************************************************************/
/** PINT_capcache_get_index
 * Determine the hash table index based on certian PVFS_capability fields.
 * Returns index of hash table.
 */
static uint16_t PINT_capcache_get_index(const PVFS_capability *cap)
{
    PFI
    uint32_t seed = 42; //Seed Murmur3
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;

    assert(cap);

    /* What to hash:
        issuer
        fsid
        op_mask
        handle_array
    */
    MurmurHash3_x64_128((const void *) cap->issuer,
        strlen(cap->issuer), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    MurmurHash3_x64_128((const void *) &cap->fsid,
        sizeof(PVFS_fs_id), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    MurmurHash3_x64_128((const void *) &cap->op_mask,
        sizeof(uint32_t), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    MurmurHash3_x64_128((const void *) cap->handle_array,
        sizeof(PVFS_handle) * cap->num_handles, seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    gossip_debug(GOSSIP_CAPCACHE_DEBUG,
        "murmur 3 x64_128: %016llx %016llx\n",
        (long long unsigned int) hash1[0],
        (long long unsigned int) hash1[1]);

    index = (uint16_t) (hash1[0] % CAPCACHE_HASH_MAX);

    /* TODO: remove these later */
    assert(index >= 0);
    assert(index <= (CAPCACHE_HASH_MAX - 1));

    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tindex=%hu\n", index);
    return index;
}

/** PINT_capcache_capability_cmp
 * Compares two PVFS_capability structures.
 * Returns 1 if the capabilities are equivalent.
 * Returns 0 otherwise.
 */
static int PINT_capcache_capability_cmp(void * cap1, void * cap2)
{
    PFI
    int rc = 0;
    /* TODO */
    return rc;
}

/** PINT_capcache_reset_stats
 * Resets the Capability Cache Statistics.
 * This function is not MT/MP safe.
 */
static void PINT_capcache_reset_stats(struct capcache_stats_s const *stats)
{
    PFI
    memset((void *) stats, 0, sizeof(struct capcache_stats_s));
}

/** PINT_capcache_convert_capability
 * Converts a capability to a Capability Cache Entry.
 * Returns 0 on success.
 * Return negative PVFS error on failure.
 */
static int PINT_capcache_convert_capability(
    const PVFS_capability * cap,
    struct capcache_entry_s *entry)
{
    PFI
    if(!entry || !cap)
    {
        return -PVFS_EINVAL;
    }

    entry->expiration = time(NULL) + capcache->default_timeout_length;

    /* Mallocs space to copy capability, free later */
    entry->cap = PINT_dup_capability(cap);
    if(!entry->cap)
    {
        return -PVFS_ENOMEM;
    }
    return 0;
}

/** Frees allocated members of capcache_entry_s and then frees the entry
 *
 */
void PINT_capcache_cleanup_entry(void * entry)
{
    if(entry)
    {
        if(((struct capcache_entry_s *) entry)->cap)
        {
            gossip_debug(GOSSIP_CAPCACHE_DEBUG,
                "\tCleaning up capability: %llu\n",
                (long long unsigned int) entry);
            PINT_cleanup_capability(((struct capcache_entry_s *) entry)->cap);
        }
        free(entry);
    }
}

/* LOCKING */
/** Initializes the proper lock based on the LOCK_TYPE
 * Returns 0 on success, -1 on error
 */
static int lock_init(capcache_lock_t * lock)
{
    PFI
    int rc = -1;
    /* TODO: ability to disable locking */
#if LOCK_TYPE == 0
    return 0;
#elif LOCK_TYPE == 1
    pthread_mutexattr_t attr;
    rc = pthread_mutexattr_init(&attr);
    if (rc != 0) return -1;
    rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (rc != 0) return -1;
    rc = pthread_mutex_init(lock, &attr);
    if (rc != 0) return -1;
#elif LOCK_TYPE == 2
    rc = pthread_spin_init(lock, 1);
    if (rc != 0) return -1;
#elif LOCK_TYPE == 3
    *lock = (capcache_lock_t) GEN_SHARED_MUTEX_INITIALIZER_NP;
    rc = 0;
#endif
    return rc;
}

/** Acquires the lock referenced by lock and returns 0 on Success;
 * otherwise, returns -1 and sets errno.
 */
static inline int lock_lock(capcache_lock_t * lock)
{
    PFI
    int rc = 0;
#if LOCK_TYPE == 0
    return rc;
#elif LOCK_TYPE == 1
    rc = pthread_mutex_lock(lock);
    return rc;
#elif LOCK_TYPE == 2
    return pthread_spin_lock(lock);
#elif LOCK_TYPE == 3
    rc = gen_mutex_lock(lock);
    return rc;
#endif
}

/** Unlocks the lock.
 * If successful, return zero; otherwise, return -1 and sets errno.
 */
static inline int lock_unlock(capcache_lock_t * lock)
{
    PFI
#if LOCK_TYPE == 0
    return 0;
#elif LOCK_TYPE == 1
    return pthread_mutex_unlock(lock);
#elif LOCK_TYPE == 2
    return pthread_spin_unlock(lock);
#elif LOCK_TYPE == 3
    return gen_mutex_unlock(lock);
#endif
}

/** Tries the lock to see if it's available:
 * Returns 0 if lock has not been acquired ie: success
 * Otherwise, returns -1
 */
static inline int lock_trylock(capcache_lock_t * lock)
{
    PFI
    int rc = -1;
#if (LOCK_TYPE == 0)
    return 0;
#elif (LOCK_TYPE == 1)
    rc = pthread_mutex_trylock(lock);
    if(rc != 0)
    {
        rc = -1;
    }
#elif (LOCK_TYPE == 2)
    rc = pthread_spin_trylock(lock);
    if(rc != 0)
    {
        rc = -1;
    }
#elif LOCK_TYPE == 3
    rc = gen_mutex_trylock(lock);
    if (rc != 0)
    {
        rc = -1;
    }
#endif
    if(rc == 0)
    {
        /* Unlock before leaving if lock wasn't already set */
        rc = lock_unlock(lock);
    }
    return rc;
}
#endif /* OFS_CAPCACHE_ENABLE */
