/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Capability cache functions
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CAPCACHE

#include <malloc.h>
#include <string.h>

#include "capcache.h"
#include "security-util.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "murmur3.h"
#include "gossip.h"
#include "pvfs2-debug.h"

/* global variables */
struct capcache_s * capcache = NULL;
capcache_lock_t lock;

/* sizes of structures/types */
uint64_t capcache_s_size = sizeof(struct capcache_s);
uint64_t capcache_stats_s_size = sizeof(struct capcache_stats_s);
uint64_t capcache_entry_s_size = sizeof(struct capcache_entry_s);
uint64_t capcache_lock_t_size = sizeof(capcache_lock_t);

/* internal-only capability cache functions */
static void PINT_capcache_debug_capability(const PVFS_capability *cap,
    const char *prefix);
static int PINT_capcache_entry_expired(void *e1, void *e2);
static void PINT_capcache_rm_expired_entries(PVFS_boolean all, uint16_t index );
static uint16_t PINT_capcache_get_index(const PVFS_capability *cap);
static int PINT_capcache_capability_cmp (
    void * cap,
    void * entry);
static int PINT_capcache_capability_quick_cmp(
    void *cap,
    void *entry);
static void PINT_capcache_reset_stats(struct capcache_stats_s const *stats);
static int PINT_capcache_convert_capability(
    const PVFS_capability * cap,
    struct capcache_entry_s * entry);
static void PINT_capcache_cleanup_entry(void *);
static int lock_init(capcache_lock_t * lock);
static inline int lock_lock(capcache_lock_t * lock);
static inline int lock_unlock(capcache_lock_t * lock);
static inline int lock_trylock(capcache_lock_t * lock);
/* END of internal-only capability cache functions */

/* PINT_capcache_debug_capability
 *
 * Outputs the fields of a capability.
 * prefix should typically be "caching" or "removing".
 */
static void PINT_capcache_debug_capability(
    const PVFS_capability *cap,
    const char *prefix)
{
    char sig_buf[10], mask_buf[10];
    int i;

    if (strlen(cap->issuer) == 0)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s null capability\n", prefix);
        return;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s capability:\n", prefix);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tissuer: %s\n", cap->issuer);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tfsid: %u\n", cap->fsid);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tsig_size: %u\n", cap->sig_size);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tsignature: %s\n",
                 PINT_util_bytes2str(cap->signature, sig_buf, 4));
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\ttimeout: %d\n",
                 (int) cap->timeout);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\top_mask: %s\n",
                 PINT_print_op_mask(cap->op_mask, mask_buf));
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tnum_handles: %u\n",
                 cap->num_handles);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tfirst handle: %llu\n",
                 cap->num_handles > 0 ? llu(cap->handle_array[0]) : 0LL);
    for (i = 1; i < cap->num_handles; i++)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "\thandle %d: %llu\n",
                     i+1, llu(cap->handle_array[i]));
    }
}

static void PINT_capcache_print_stats(void)
{
    static int count = 0;

    count++;
    if (count % CAPCACHE_STATS_FREQ == 0)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** capability cache statistics "
                     "***\n");
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** entries: %llu inserts: %llu "
                     "removes: %llu\n", llu(capcache->stats->entry_cnt), 
                     llu(capcache->stats->inserts), 
                     llu(capcache->stats->removed));
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** lookups: %llu hits: %llu (%3.1f%%) misses: "
                     "%llu (%3.1f%%) expired: %llu\n", llu(capcache->stats->lookups), 
                     llu(capcache->stats->hits),
                     ((float) capcache->stats->hits / capcache->stats->lookups * 100),
                     llu(capcache->stats->misses),
                     ((float) capcache->stats->misses / capcache->stats->lookups * 100),
                     llu(capcache->stats->expired));
        count = 0;
    }
}

/** Initializes the capability cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_init(void)
{
    int i, hash_index;

    CAPCACHE_ENTER_FN();

    /* Initialize capability cache lock */
    lock_init(&lock);
    /* Acquire the lock */
    lock_lock(&lock);

    /* Acquire memory */
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "malloc capcache_s_size = %llu\n",
        (long long unsigned int) capcache_s_size);
    capcache = (capcache_t *) malloc(capcache_s_size);
    if ((void *) capcache == NULL)
    {
        /* Unlock the capability cache lock */
        lock_unlock(&lock);
        return -PVFS_ENOMEM;
    }
    /* Clear the ucache_s structure */
    memset(capcache, 0, capcache_s_size);
    /* Cache structure allocation successful */
    capcache->size_limit = CAPCACHE_SIZE_CAP;
    capcache->default_timeout_length = CAPCACHE_TIMEOUT;
    capcache->entry_limit = CAPCACHE_ENTRY_LIMIT;
    gossip_debug(GOSSIP_SECCACHE_DEBUG,
        "malloc capcache_stats_s_size = %llu\n",
        (long long unsigned int) capcache_stats_s_size);
    capcache->stats = (struct capcache_stats_s *) malloc(
        capcache_stats_s_size);
    if (capcache->stats == NULL)
    {
        goto destroy_capcache;
    }
    PINT_capcache_reset_stats(capcache->stats);
    capcache->stats->cache_size = capcache_s_size;

    gossip_debug(GOSSIP_SECCACHE_DEBUG,
        "initializing the heads of hash table chains\n");
    
    for (i = 0; i < CAPCACHE_HASH_MAX; i++)
    {
        /* note a malloc is performed, don't forget to free up mem */
        capcache->entries[i] = PINT_llist_new();
        if (capcache->entries[i] == NULL)
        {
            /* recover */
            goto destroy_head_entries;
        }
    }

    /* allocate space for hash chain head entries */
    for (hash_index = 0; hash_index < CAPCACHE_HASH_MAX; hash_index++)
    {
        struct capcache_entry_s * entry_p = NULL;
        entry_p = (struct capcache_entry_s *) malloc(capcache_entry_s_size);
        if (capcache->entries[hash_index] == NULL)
        {
            goto destroy_head_entries;
        }
        memset(entry_p, 0, capcache_entry_s_size);
        entry_p->expiration = 0xFFFFFFFF;
        PINT_llist_add_to_head(capcache->entries[hash_index],
            (void *) entry_p);
    }
    /* unlock the capability cache Lock */
    lock_unlock(&lock);
    return 0;

destroy_head_entries:
    for (hash_index = 0; hash_index < CAPCACHE_HASH_MAX; hash_index++)
    {
        PINT_llist_free(capcache->entries[hash_index],
            &PINT_capcache_cleanup_entry);
    }
    if (capcache->stats != NULL)
    {
        free(capcache->stats);
    }
destroy_capcache:
    if (capcache != NULL)
    {
        free(capcache);
    }
    /* unlock the capability cache lock */
    lock_unlock(&lock);

    CAPCACHE_EXIT_FN();

    return -PVFS_ENOMEM;
}


/** Releases resources used by the capability cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_finalize(void)
{
    int ret = 0, hash_index;

    CAPCACHE_ENTER_FN();
    ret = lock_lock(&lock);
    if (ret != 0)
    {
        return -PVFS_ENOLCK;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "Finalizing capability cache...\n");

    if (capcache != NULL)
    {
        if (capcache->stats != NULL)
        {
            free(capcache->stats);
        }
        /* Loop over capcache entries, freeing entry payload and entry */        
        for (hash_index = 0; hash_index < CAPCACHE_HASH_MAX; hash_index++)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG,
                "Freeing entries in hash table chain at index = %d\n",
                hash_index);
            PINT_llist_free(capcache->entries[hash_index],
                &PINT_capcache_cleanup_entry);
        }
        free (capcache);
        capcache = NULL;
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "capability cache freed...\n");
    }
    ret = lock_unlock(&lock);

    CAPCACHE_EXIT_FN();

    if (ret != 0)
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
static int PINT_capcache_entry_expired(void *e1, void *e2)
{
    if (((struct capcache_entry_s *) e1)->expiration >=
       ((struct capcache_entry_s *) e2)->expiration)
    {
        /* entry has expired */
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
static void PINT_capcache_rm_expired_entries(PVFS_boolean all, uint16_t index)
{
    struct capcache_entry_s now;
    int hash_index;

    CAPCACHE_ENTER_FN();

    lock_lock(&lock);
    now.expiration = time(NULL);
    /* removes expired entries from all hash table chains */
    if (all)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG,
            "Removing all entries with timeouts before %llu\n",
            (long long unsigned int) now.expiration);        
        for (hash_index = 0; hash_index < CAPCACHE_HASH_MAX; hash_index++)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG,
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
                capcache->stats->removed++;
                capcache->stats->entry_cnt--;
            }
        }
    }
    else
    {
        /* remove expired entries from the hash table chain at index */
        gossip_debug(GOSSIP_SECCACHE_DEBUG,
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
            capcache->stats->removed++;
            capcache->stats->entry_cnt--;
        }
    }
    lock_unlock(&lock);

    CAPCACHE_EXIT_FN();
}

/** Lookup
 * Returns pointer to capability cache Entry on success.
 * Returns NULL on failure.
 */
struct capcache_entry_s * PINT_capcache_lookup_entry(PVFS_capability * cap)
{
    struct capcache_entry_s * current = NULL, now;
    uint16_t index = 0;

    CAPCACHE_ENTER_FN();

    if (capcache == NULL || cap == NULL)
    {
        return NULL;
    }    

    /* get index of the hash table chain to look in */
    index = PINT_capcache_get_index(cap);

    /* acquire the lock */
    lock_lock(&lock);

    /* iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    current = (struct capcache_entry_s *) PINT_llist_search(
        capcache->entries[index],
        cap,
        &PINT_capcache_capability_cmp);
    
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "cap cache %s!\n",
                 (current != NULL) ? "hit" : "miss");    

    /* unlock the capability cache lock */
    lock_unlock(&lock);

    /* check expiration */
    if (current != NULL)
    {        
        now.expiration = time(NULL);
        /* 0 returned if expired */
        if (PINT_capcache_entry_expired(&now, current) == 0)
        {            
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "entry %p expired\n", current);
            PINT_capcache_remove_entry(current);
            current = NULL;
            capcache->stats->expired++;
        }
        else
        {
            /* update expiration -- but not past cap timeout */
            current->expiration = now.expiration + capcache->default_timeout_length;
            if (current->cap->timeout < current->expiration)
            {
                current->expiration = current->cap->timeout;
            }
        }
        capcache->stats->hits++;
    }
    else
    {
        capcache->stats->misses++;
    }

    capcache->stats->lookups++;
    PINT_capcache_print_stats();

    CAPCACHE_EXIT_FN();

    return current;
}

/** PINT_capcache_insert_entry
 * Inserts 'cap' into the capability cache.
 * Returns 0 on succes; otherwise returns -PVFS_error.
 */
int PINT_capcache_insert_entry(const PVFS_capability * cap)
{
    int ret = 0;
    uint16_t index = 0;
    struct capcache_entry_s * entry = NULL;

    CAPCACHE_ENTER_FN();

    if (capcache == NULL || cap == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* Determine which hash table chain we'll be working with */
    index = PINT_capcache_get_index(cap);

    /* Remove expired capabilities in this chain */
    PINT_capcache_rm_expired_entries(0, index);

    entry = (struct capcache_entry_s *) malloc(sizeof(struct capcache_entry_s));
    if (entry == NULL)
    {
        return -PVFS_ENOMEM;
    }

    ret = PINT_capcache_convert_capability(cap, entry);
    if (ret != 0)
    {
        ret = -PVFS_ENOMEM;
        goto destroy_entry;
    }

    PINT_capcache_debug_capability(entry->cap, "Caching");

    /* acquire the lock */
    lock_lock(&lock);

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "entry %p added to the head of the "
                 "linked list @ index = %d\n", entry, index);
    if (PINT_llist_add_to_head(capcache->entries[index], entry) < 0)
    {
        ret = -PVFS_ENOMEM;
        goto destroy_entry;
    }

    /* unlock the capability cache lock */
    lock_unlock(&lock);

    capcache->stats->inserts++;
    capcache->stats->entry_cnt++;

    CAPCACHE_EXIT_FN();

    return 0;

destroy_entry:
    free(entry);

    CAPCACHE_EXIT_FN();

    return ret;
}

/* Remove */
int PINT_capcache_remove_entry(struct capcache_entry_s *entry)
{
    int ret = 0;
    uint16_t index = 0;
    struct capcache_entry_s *rem;

    CAPCACHE_ENTER_FN();

    /* lock capability cache */
    lock_lock(&lock);
        
    index = PINT_capcache_get_index(entry->cap);
    rem = (struct capcache_entry_s *) 
              PINT_llist_rem(capcache->entries[index],
                             entry->cap,
                             &PINT_capcache_capability_cmp);
    
    /* unlock capability cache */
    lock_unlock(&lock);

    /* free memory */
    if (rem != NULL)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "removed entry %p at "
                     "index %hd\n", rem, index);
        if (rem->cap != NULL)
        {
            PINT_cleanup_capability(rem->cap);
        }
        free(rem);
        capcache->stats->removed++;
        capcache->stats->entry_cnt--;
    }

    CAPCACHE_EXIT_FN();
    return ret;
}

/** PINT_capcache_quick_sign
 * Copy signature from cached capability if fields match
 */
int PINT_capcache_quick_sign(PVFS_capability *cap)
{
    struct capcache_entry_s * current = NULL;
    uint16_t index = 0;

    CAPCACHE_ENTER_FN();

    if (capcache == NULL || cap == NULL)
    {
        return -PVFS_EINVAL;
    }    

    /* acquire the lock */
    lock_lock(&lock);

    /* get index of the hash table chain to look in */
    index = PINT_capcache_get_index(cap);
    
    /* iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    current = (struct capcache_entry_s *) PINT_llist_search(
        capcache->entries[index],
        cap,
        &PINT_capcache_capability_quick_cmp);

    /* release the lock */
    lock_unlock(&lock);

    if (current != NULL)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: entry found!\n",
                     __func__);
    }
    else
    {
        CAPCACHE_EXIT_FN();
        return 1;
    }

    /* copy capability timeout & signature */
    cap->timeout = current->cap->timeout;
    cap->sig_size = current->cap->sig_size;
    memcpy(cap->signature, current->cap->signature, current->cap->sig_size);

    CAPCACHE_EXIT_FN();

    return 0;
}

/*****************************************************************************/

/** PINT_capcache_get_index
 * Determine the hash table index based on certain
 * PVFS_capability fields. Returns index of hash table.
 */
static uint16_t PINT_capcache_get_index(const PVFS_capability *cap)
{    
    uint32_t seed = 42; //Seed Murmur3
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;

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

    index = (uint16_t) (hash1[0] % CAPCACHE_HASH_MAX);

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tindex=%hu\n", index);

    return index;
}

/** PINT_capcache_capability_cmp
 * Compares two PVFS_capability structures. Returns 0 if the
 * capabilities are equivalent. Returns nonzero otherwise.
 */
static int PINT_capcache_capability_cmp(void *cap, void *entry)
{
    PVFS_capability *kcap, *ecap;
    struct capcache_entry_s *pentry;

    pentry = (struct capcache_entry_s *) entry;

    /* ignore chain end marker */
    if (pentry->cap == NULL)
    {
        return 1;
    }

    ecap = pentry->cap;
    kcap = (PVFS_capability *) cap;

    /* if both sig_sizes are 0, they're null caps */
    if (kcap->sig_size == 0 && ecap->sig_size == 0)
    {
        return 0;
    }
    /* sizes don't match -- shouldn't happen */
    else if (kcap->sig_size != ecap->sig_size)
    {
        gossip_err("Warning: capability cache: signature size mismatch "
                     "(key: %d   entry: %d)\n", kcap->sig_size, ecap->sig_size);
        return 1;
    }

    /* compare signatures */
    return memcmp(kcap->signature, ecap->signature, kcap->sig_size);
}

/** PINT_capcache_capability_cmp
 * Compares two PVFS_capability structures. Returns 0 if the
 * capability fields are equivalent. Returns nonzero otherwise.
 */
static int PINT_capcache_capability_quick_cmp(void *cap, void *entry)
{
    PVFS_capability *kcap, *ecap;
    struct capcache_entry_s *pentry;

    pentry = (struct capcache_entry_s *) entry;

    /* ignore chain end marker */
    if (pentry->cap == NULL)
    {
        return 1;
    }

    ecap = pentry->cap;
    kcap = (PVFS_capability *) cap;

    return (!(kcap->fsid == ecap->fsid &&
              kcap->num_handles == ecap->num_handles &&
              kcap->op_mask == ecap->op_mask &&
              (strcmp(kcap->issuer, ecap->issuer) == 0) &&
              (memcmp(kcap->handle_array, ecap->handle_array, 
                      kcap->num_handles * sizeof(PVFS_handle)) == 0)));
}

/** PINT_capcache_reset_stats
 * Resets the capability cache statistics. This function is not
 * MT/MP safe.
 */
static void PINT_capcache_reset_stats(struct capcache_stats_s const *stats)
{
    memset((void *) stats, 0, sizeof(struct capcache_stats_s));
}

/** PINT_capcache_convert_capability
 * Converts a capability to a capability cache entry.
 * Returns 0 on success.
 * Return negative PVFS error on failure.
 */
static int PINT_capcache_convert_capability(
    const PVFS_capability * cap,
    struct capcache_entry_s *entry)
{
    if (entry == NULL || cap == NULL)
    {
        return -PVFS_EINVAL;
    }

    entry->expiration = time(NULL) + capcache->default_timeout_length;

    /* expire when cap times out */
    if (cap->timeout < entry->expiration)
    {
        entry->expiration = cap->timeout;
    }

    /* mallocs space to copy capability, free later */
    entry->cap = PINT_dup_capability(cap);
    if (entry->cap == NULL)
    {
        return -PVFS_ENOMEM;
    }
    return 0;
}

/** PINT_capcache_cleanup_entry()
 *  Frees allocated members of capcache_entry_s and then frees
 *  the entry.
 */
static void PINT_capcache_cleanup_entry(void * entry)
{
    if (entry != NULL)
    {
        if (((struct capcache_entry_s *) entry)->cap != NULL)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG,
                "\tCleaning up capability: %llu\n",
                (long long unsigned int) entry);
            PINT_cleanup_capability(((struct capcache_entry_s *) entry)->cap);
        }
        free(entry);
    }
}

/* LOCKING */
/** lock_init() 
 * Initializes the proper lock based on the CAPCACHE_LOCK_TYPE
 * Returns 0 on success, -1 on error
 */
static int lock_init(capcache_lock_t * lock)
{
    int ret = -1;

    /* TODO: ability to disable locking */
#if CAPCACHE_LOCK_TYPE == 0
    return 0;
#elif CAPCACHE_LOCK_TYPE == 1
    pthread_mutexattr_t attr;
    ret = pthread_mutexattr_init(&attr);
    if (ret != 0) return -1;
    ret = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (ret != 0) return -1;
    ret = pthread_mutex_init(lock, &attr);
    if (ret != 0) return -1;
#elif CAPCACHE_LOCK_TYPE == 2
    ret = pthread_spin_init(lock, 1);
    if (ret != 0) return -1;
#elif CAPCACHE_LOCK_TYPE == 3
    *lock = (capcache_lock_t) GEN_SHARED_MUTEX_INITIALIZER_NP;
    ret = 0;
#endif
    return ret;
}

/** lock_lock()
 * Acquires the lock referenced by lock and returns 0 on
 * Success; otherwise, returns -1 and sets errno.
 */
static inline int lock_lock(capcache_lock_t * lock)
{
    int ret = 0;
    
#if CAPCACHE_LOCK_TYPE == 0
    return ret;
#elif CAPCACHE_LOCK_TYPE == 1
    ret = pthread_mutex_lock(lock);
    return ret;
#elif CAPCACHE_LOCK_TYPE == 2
    return pthread_spin_lock(lock);
#elif CAPCACHE_LOCK_TYPE == 3
    ret = gen_mutex_lock(lock);
    return ret;
#endif
}

/** lock_unlock()
 * Unlocks the lock.
 * If successful, return zero; otherwise, return -1 and sets errno.
 */
static inline int lock_unlock(capcache_lock_t * lock)
{
#if CAPCACHE_LOCK_TYPE == 0
    return 0;
#elif CAPCACHE_LOCK_TYPE == 1
    return pthread_mutex_unlock(lock);
#elif CAPCACHE_LOCK_TYPE == 2
    return pthread_spin_unlock(lock);
#elif CAPCACHE_LOCK_TYPE == 3
    return gen_mutex_unlock(lock);
#endif
}

/** lock_trylock
 * Tries the lock to see if it's available:
 * Returns 0 if lock has not been acquired ie: success
 * Otherwise, returns -1
 */
static inline int lock_trylock(capcache_lock_t * lock)
{
    int ret = -1;

#if (CAPCACHE_LOCK_TYPE == 0)
    return 0;
#elif (CAPCACHE_LOCK_TYPE == 1)
    ret = pthread_mutex_trylock(lock);
    if (ret != 0)
    {
        ret = -1;
    }
#elif (CAPCACHE_LOCK_TYPE == 2)
    ret = pthread_spin_trylock(lock);
    if (ret != 0)
    {
        ret = -1;
    }
#elif CAPCACHE_LOCK_TYPE == 3
    ret = gen_mutex_trylock(lock);
    if (ret != 0)
    {
        ret = -1;
    }
#endif
    if (ret == 0)
    {
        /* Unlock before leaving if lock wasn't already set */
        ret = lock_unlock(lock);
    }
    return ret;
}
#endif /* ENABLE_CAPCACHE */
