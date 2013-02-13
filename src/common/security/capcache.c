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
#include <gossip.h>
#include <pvfs2-debug.h>

/* Global Variables */
struct capcache_s * capcache = NULL;
capcache_lock_t lock;

/* Internal Only Capability Cache Functions */
static uint16_t PINT_capcache_get_index(const PVFS_capability *cap);
static PVFS_boolean PINT_capcache_capability_cmp (
    PVFS_capability * cap1,
    PVFS_capability * cap2);
static void PINT_capcache_reset_stats(struct capcache_stats_s const *stats);
static int PINT_capcache_convert_capability(
    const PVFS_capability * cap,
    struct capcache_entry_s * entry);
static int lock_init(capcache_lock_t * lock);
static inline int lock_lock(capcache_lock_t * lock);
static inline int lock_unlock(capcache_lock_t * lock);
static inline int lock_trylock(capcache_lock_t * lock);
/* END of Internal Only Capability Cache Functions */

/** Initializes the Capability Cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_init(void)
{
    PFI

    /* Acquire Memory */
    capcache = (capcache_t *) malloc(sizeof(struct capcache_s));
    if ((void *) capcache == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* Initialize Capability Cache Lock */
    lock_init(&lock);

    /* Acquire the lock */
    lock_lock(&lock);

    /* Cache Structure allocation successful */
    capcache->size_limit = CAPCACHE_SIZE_CAP;
    capcache->default_timeout_length = CAPCACHE_TIMEOUT;
    capcache->entry_limit = CAPCACHE_ENTRY_LIMIT;
    capcache->stats = (struct capcache_stats_s *) malloc(sizeof(struct capcache_stats_s));
    if(!capcache->stats)
    {
        goto destroy_capcache;
    }
    PINT_capcache_reset_stats(capcache->stats);
    capcache->stats->cache_size = sizeof(struct capcache_s);
    memset(capcache->entries, 0, sizeof(struct capcache_entry_s *) * CAPCACHE_HASH_MAX);

    /* Allocate space for hash chain head entries */
    int hash_index = 0;
    for(; hash_index < CAPCACHE_HASH_MAX; hash_index++)
    {
        capcache->entries[hash_index] = (struct capcache_entry_s *) malloc(sizeof(struct capcache_entry_s));
        if(!capcache->entries[hash_index])
        {
            goto destroy_head_entries;
        }
    }

    /* Unlock the Capability Cache Lock */
    lock_unlock(&lock);

    return 0;

destroy_head_entries:
    hash_index = 0;
    for(; hash_index < CAPCACHE_HASH_MAX; hash_index++)
    {
        if (capcache->entries[hash_index])
        {
            free(capcache->entries);
        }
        else
        {
            break;
        }
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
            //struct capcache_entry_s *current = NULL;
        }

        free (capcache);
        gossip_debug(GOSSIP_CAPCACHE_DEBUG, "Capability Cache freed...\n");
    }
    rc = lock_unlock(&lock);
    if(rc != 0)
    {
        return -PVFS_ENOLCK;
    }
    return 0;
}

/* Perform some function on every entry in our HT */
int PINT_capcache_forall(int (*function)(struct capcache_entry_s *))
{
    PFI

    int rc = 0;

    if(!function || !capcache)
    {
        return -PVFS_EINVAL;
    }

    uint16_t index = 0;
    for(; index < CAPCACHE_HASH_MAX; index++)
    {
        struct capcache_entry_s * current = capcache->entries[index];
        while(current)
        {
            rc = function(current);
            current = current->next;
        }
    }
    return 0;
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
     * is found */
    current = capcache->entries[index];
    while(current)
    {
        if(PINT_capcache_capability_cmp(cap, current->cap))
        {
            /* MATCH FOUND */
            break;
        }
        current = current->next; /* Keep looking... */
    }

    /* Unlock the Capability Cache Lock */
    lock_unlock(&lock);

    return current;
}

/* Insert */
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

    /* Malloc New Entry for now, eventually might want to preallocate */
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

    index = PINT_capcache_get_index(cap);

    /* Acquire the lock */
    lock_lock(&lock);

    if(capcache->entries[index])
    {
        entry->next = capcache->entries[index];
    }
    /* Insert at head of chain */
    capcache->entries[index] = entry;

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
    return rc;
}

/*****************************************************************************/
/** Determine the hash table index based on the signature of a capability.
 * Returns index on success.
 */
static uint16_t PINT_capcache_get_index(const PVFS_capability *cap)
{
    PFI

    uint16_t index = 0;

    /* TODO: remove this later */
    assert(cap);

    if(cap->sig_size == 0 || !cap->signature)
    {
        /* TODO: compute hash based on some unique data of capability other than signature */
        /* This will be the case when no form of security is enabled */
        index = 0;
        gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tinvalid signature -> index=%hu\n", index);
        return index;
    }

    /* TODO: remove this later */
    assert(cap->sig_size >= 8);
    index = (uint16_t) (*((uint64_t *) cap->signature) % CAPCACHE_HASH_MAX);

    /* TODO: remove these later */
    assert(index >= 0);
    assert(index <= (CAPCACHE_HASH_MAX - 1));
    gossip_debug(GOSSIP_CAPCACHE_DEBUG, "\tindex=%hu\n", index);
    return index;
}

/** Compares two PVFS_capability structures.
 * Returns 1 if the capabilities' signatures are equivalent.
 * Returns 0 otherwise.
 */
static PVFS_boolean PINT_capcache_capability_cmp(PVFS_capability * cap1, PVFS_capability * cap2)
{
    PFI
    PVFS_boolean rc = 1;

    if (memcmp(cap1->signature, cap2->signature, cap1->sig_size) == 0)
    {
        rc = (PVFS_boolean) 0;
    }
    else
    {
        rc = (PVFS_boolean) 1;
    }
    return rc;
}

/** Resets the Capability Cache Statistics.
 * This function is not MT/MP safe.
 */
static void PINT_capcache_reset_stats(struct capcache_stats_s const *stats)
{
    PFI
    memset((void *) stats, 0, sizeof(struct capcache_stats_s));
}

/** Converts a capability to a Capability Cache Entry.
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

    entry->next = (struct capcache_entry_s *) NULL;
    entry->expiration = time(NULL) + capcache->default_timeout_length;

    /* Mallocs space to copy capability, free later */
    entry->cap = PINT_dup_capability(cap);
    if(!entry->cap)
    {
        return -PVFS_ENOMEM;
    }
    return 0;
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
    *lock = (capcache_lock_t) GEN_SHARED_MUTEX_INITIALIZER_NP; //GEN_SHARED_MUTEX_INITIALIZER_NP;
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
