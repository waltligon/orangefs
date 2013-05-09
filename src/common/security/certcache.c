/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Certificate cache functions
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CERTCACHE

#include <malloc.h>
#include <string.h>

#include <openssl/x509.h>
#include <openssl/asn1.h>

#include "certcache.h"
#include "server-config.h"
#include "security-util.h"
#include "cert-util.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "murmur3.h"
#include "gossip.h"
#include "pvfs2-debug.h"

/* global variables */
struct certcache_s * certcache = NULL;
certcache_lock_t lock;

/* sizes of structures/types */
uint64_t certcache_s_size = sizeof(struct certcache_s);
uint64_t certcache_stats_s_size = sizeof(struct certcache_stats_s);
uint64_t certcache_entry_s_size = sizeof(struct certcache_entry_s);
uint64_t certcache_lock_t_size = sizeof(certcache_lock_t);

/* internal-only certificate cache functions */
static void PINT_certcache_debug_certificate(const PVFS_certificate *cert,
    const char *prefix);
static int PINT_certcache_entry_expired(void *e1, void *e2);
static void PINT_certcache_rm_expired_entries(PVFS_boolean all, uint16_t index);
static uint16_t PINT_certcache_get_index(const char *subject);
static int PINT_certcache_certificate_cmp (void *cert,
    void *entry);
static int PINT_certcache_subject_cmp(void *subject, 
    void *entry);
static void PINT_certcache_reset_stats(struct certcache_stats_s const *stats);
static int PINT_certcache_convert_certificate(const PVFS_certificate *cert,
    PVFS_uid uid,
    uint32_t num_groups,
    PVFS_gid *group_array,
    struct certcache_entry_s *entry);
static void PINT_certcache_cleanup_entry(void *);
static void get_cert_subject(const PVFS_certificate *cert, char *subject);
static PVFS_time get_expiration(const PVFS_certificate *cert);
static int lock_init(certcache_lock_t *lock);
static inline int lock_lock(certcache_lock_t *lock);
static inline int lock_unlock(certcache_lock_t *lock);
static inline int lock_trylock(certcache_lock_t *lock);
/* END of internal-only certificate cache functions */

/* get_cert_subject
 * 
 * Get subject of certificate
 */
static void get_cert_subject(const PVFS_certificate *cert, char *subject)
{
    X509 *xcert = NULL;
    X509_NAME *xsubject;
    
    if (PINT_cert_to_X509(cert, &xcert) != 0)
    {
        return;
    }

    xsubject = X509_get_subject_name(xcert);
    if (xsubject == NULL)
    {
        return;
    }

    X509_NAME_oneline(xsubject, subject, CERTCACHE_SUBJECT_SIZE);
    subject[CERTCACHE_SUBJECT_SIZE-1] = '\0';
}

/* get_expiration 
 * 
 * Return expiration time of entry -- return 0 on error
 * or if cert is within the security timeout of expiration
 */
static PVFS_time get_expiration(const PVFS_certificate *cert)
{
    X509 *xcert = NULL;
    ASN1_UTCTIME *certexp;
    PVFS_time entryexp;

    if (PINT_cert_to_X509(cert, &xcert) != 0)
    {
        return 0;
    }

    if ((certexp = X509_get_notAfter(xcert)) == NULL)
    {
        return 0;
    }

    entryexp = time(NULL) + certcache->default_timeout_length;
    return (ASN1_UTCTIME_cmp_time_t(certexp, entryexp) == -1) ? 0 : entryexp;
}

/* PINT_certcache_debug_certificate
 *
 * Outputs the subject of a certificate.
 * prefix should typically be "caching" or "removing".
 */
static void PINT_certcache_debug_certificate(
    const PVFS_certificate *cert,
    const char *prefix)
{
    char subject[CERTCACHE_SUBJECT_SIZE];

    get_cert_subject(cert, subject);

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s certificate:\n", prefix);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tsubject: %s\n", subject);
}

static void PINT_certcache_print_stats(void)
{
    static int count = 0;

    if (++count % CERTCACHE_STATS_FREQ == 0)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** certificate cache statistics "
                     "***\n");
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** entries: %llu inserts: %llu "
                     "removes: %llu\n", llu(certcache->stats->entry_cnt), 
                     llu(certcache->stats->inserts), 
                     llu(certcache->stats->removed));
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** lookups: %llu hits: %llu (%3.1f%%) misses: "
                     "%llu (%3.1f%%) expired: %llu\n", llu(certcache->stats->lookups), 
                     llu(certcache->stats->hits),
                     ((float) certcache->stats->hits / certcache->stats->lookups * 100),
                     llu(certcache->stats->misses),
                     ((float) certcache->stats->misses / certcache->stats->lookups * 100),
                     llu(certcache->stats->expired));
        count = 0;
    }
}

/** Initializes the certificate cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_certcache_init(void)
{
    int i, hash_index;
    struct server_configuration_s *config;

    CERTCACHE_ENTER_FN();

    config = PINT_get_server_config();

    /* Initialize certificate cache lock */
    lock_init(&lock);
    /* Acquire the lock */
    lock_lock(&lock);

    /* Acquire memory */
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "malloc certcache_s_size = %llu\n",
        (long long unsigned int) certcache_s_size);
    certcache = (certcache_t *) malloc(certcache_s_size);
    if ((void *) certcache == NULL)
    {
        /* Unlock the certificate cache lock */
        lock_unlock(&lock);
        return -PVFS_ENOMEM;
    }
    /* Clear the ucache_s structure */
    memset(certcache, 0, certcache_s_size);
    /* Cache structure allocation successful */
    certcache->size_limit = CERTCACHE_SIZE_CAP;
    certcache->default_timeout_length = config->security_timeout;
    certcache->entry_limit = CERTCACHE_ENTRY_LIMIT;
    gossip_debug(GOSSIP_SECCACHE_DEBUG,
        "malloc certcache_stats_s_size = %llu\n",
        (long long unsigned int) certcache_stats_s_size);
    certcache->stats = (struct certcache_stats_s *) malloc(
        certcache_stats_s_size);
    if (certcache->stats == NULL)
    {
        goto destroy_certcache;
    }
    PINT_certcache_reset_stats(certcache->stats);
    certcache->stats->cache_size = certcache_s_size;

    gossip_debug(GOSSIP_SECCACHE_DEBUG,
        "initializing the heads of hash table chains\n");
    
    for (i = 0; i < CERTCACHE_HASH_MAX; i++)
    {
        /* note a malloc is performed, don't forget to free up mem */
        certcache->entries[i] = PINT_llist_new();
        if (certcache->entries[i] == NULL)
        {
            /* recover */
            goto destroy_head_entries;
        }
    }

    /* allocate space for hash chain head entries */
    for (hash_index = 0; hash_index < CERTCACHE_HASH_MAX; hash_index++)
    {
        struct certcache_entry_s * entry_p = NULL;
        entry_p = (struct certcache_entry_s *) malloc(certcache_entry_s_size);
        if (certcache->entries[hash_index] == NULL)
        {
            goto destroy_head_entries;
        }
        memset(entry_p, 0, certcache_entry_s_size);
        entry_p->expiration = 0xFFFFFFFF;
        PINT_llist_add_to_head(certcache->entries[hash_index],
            (void *) entry_p);
    }
    /* unlock the certificate cache Lock */
    lock_unlock(&lock);
    return 0;

destroy_head_entries:
    for (hash_index = 0; hash_index < CERTCACHE_HASH_MAX; hash_index++)
    {
        PINT_llist_free(certcache->entries[hash_index],
            &PINT_certcache_cleanup_entry);
    }
    if (certcache->stats != NULL)
    {
        free(certcache->stats);
    }
destroy_certcache:
    if (certcache != NULL)
    {
        free(certcache);
    }
    /* unlock the certificate cache lock */
    lock_unlock(&lock);

    CERTCACHE_EXIT_FN();

    return -PVFS_ENOMEM;
}


/** Releases resources used by the certificate cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_certcache_finalize(void)
{
    int ret = 0, hash_index;

    CERTCACHE_ENTER_FN();
    ret = lock_lock(&lock);
    if (ret != 0)
    {
        return -PVFS_ENOLCK;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "Finalizing certificate cache...\n");

    if (certcache != NULL)
    {
        if (certcache->stats != NULL)
        {
            free(certcache->stats);
        }
        /* Loop over certcache entries, freeing entry payload and entry */        
        for (hash_index = 0; hash_index < CERTCACHE_HASH_MAX; hash_index++)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG,
                "Freeing entries in hash table chain at index = %d\n",
                hash_index);
            PINT_llist_free(certcache->entries[hash_index],
                &PINT_certcache_cleanup_entry);
        }
        free (certcache);
        certcache = NULL;
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "certificate cache freed...\n");
    }
    ret = lock_unlock(&lock);

    CERTCACHE_EXIT_FN();

    if (ret != 0)
    {
        return -PVFS_ENOLCK;
    }
    return 0;
}

/** PINT_certcache_entry_expired
 * Compares certcache entry's timeout to that of a 'dummy' entry with the
 * timeout set to the current time.
 * Returns 0 if entry 'e2' has expired; otherwise, returns 1.
 */
static int PINT_certcache_entry_expired(void *e1, void *e2)
{
    if (((struct certcache_entry_s *) e1)->expiration >=
       ((struct certcache_entry_s *) e2)->expiration)
    {
        /* entry has expired */
        return 0;
    }
    return 1;
}

/** PINT_certcache_rm_expired_entries
 * If 'all' is non-zero, then all hash table chains are scanned for expired
 * entries.
 *
 * If 'all' is zero, then only the hash table chain at index is scanned for expired
 * entries.
 *
 * In both cases, an expired entry's certificate is cleaned up, the entry
 * freed, and removed from the linked list.
 */
static void PINT_certcache_rm_expired_entries(PVFS_boolean all, uint16_t index)
{
    struct certcache_entry_s now;
    int hash_index;

    CERTCACHE_ENTER_FN();

    lock_lock(&lock);
    now.expiration = time(NULL);
    /* removes expired entries from all hash table chains */
    if (all)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG,
            "Removing all entries with timeouts before %llu\n",
            (long long unsigned int) now.expiration);        
        for (hash_index = 0; hash_index < CERTCACHE_HASH_MAX; hash_index++)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG,
                "searching chain at hash_index = %d\n",
                hash_index);
            struct certcache_entry_s * entry_rm = NULL;
            while((entry_rm = PINT_llist_rem(
                certcache->entries[hash_index],
                &now,
                &PINT_certcache_entry_expired)) != NULL)
            {
                gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** removing %s (timeout: "
                             "%llu) ***\n", entry_rm->subject,
                             llu(entry_rm->expiration));
                PINT_certcache_cleanup_entry(entry_rm);
                certcache->stats->removed++;
                certcache->stats->entry_cnt--;
            }
        }
    }
    else
    {
        /* remove expired entries from the hash table chain at index */
        gossip_debug(GOSSIP_SECCACHE_DEBUG,
            "searching chain at hash_index = %d\n", index);
        struct certcache_entry_s * entry_rm = NULL;
        while((entry_rm = PINT_llist_rem(
                certcache->entries[index],
                &now,
                &PINT_certcache_entry_expired)) != NULL)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "*** removing %s ***\n",
                         entry_rm->subject);
            PINT_certcache_cleanup_entry(entry_rm);
            certcache->stats->removed++;
            certcache->stats->entry_cnt--;
        }
    }
    lock_unlock(&lock);

    CERTCACHE_EXIT_FN();
}

/** Lookup
 * Returns pointer to certificate cache Entry on success.
 * Returns NULL on failure.
 */
struct certcache_entry_s * PINT_certcache_lookup_entry(PVFS_certificate * cert)
{
    struct certcache_entry_s * current = NULL, now;
    char subject[CERTCACHE_SUBJECT_SIZE];
    uint16_t index = 0;
    PVFS_time exp;

    CERTCACHE_ENTER_FN();

    if (certcache == NULL || cert == NULL)
    {
        return NULL;
    }    

    /* get index of the hash table chain to look in */
    get_cert_subject(cert, subject);
    index = PINT_certcache_get_index(subject);

    /* acquire the lock */
    lock_lock(&lock);

    /* iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    current = (struct certcache_entry_s *) PINT_llist_search(
        certcache->entries[index],
        cert,
        &PINT_certcache_certificate_cmp);
    
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "cert cache %s!\n",
                 (current != NULL) ? "hit" : "miss");    

    /* unlock the certificate cache lock */
    lock_unlock(&lock);

    /* check expiration */
    if (current != NULL)
    {        
        now.expiration = time(NULL);
        exp = get_expiration(cert);
        /* 0 returned if expired */
        if (exp == 0 ||
            PINT_certcache_entry_expired(&now, current) == 0)
        {            
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "entry %p expired "
                         "(entry: %llu  now: %llu)\n", current, 
                         llu(current->expiration), llu(now.expiration));
            PINT_certcache_remove_entry(current);
            current = NULL;
            certcache->stats->expired++;
        }
        else
        {
            /* update expiration -- but not past cert expiration */
            current->expiration = exp;
        }
        certcache->stats->hits++;
    }
    else
    {
        certcache->stats->misses++;
    }

    certcache->stats->lookups++;
    PINT_certcache_print_stats();

    CERTCACHE_EXIT_FN();

    return current;
}

/** PINT_certcache_insert_entry
 * Inserts 'cert' into the certificate cache.
 * Returns 0 on success; otherwise returns -PVFS_error.
 */
int PINT_certcache_insert_entry(const PVFS_certificate * cert, 
                                PVFS_uid uid,
                                uint32_t num_groups,
                                PVFS_gid *group_array)
{
    int ret = 0;
    uint16_t index = 0;
    struct certcache_entry_s * entry = NULL;
    char subject[CERTCACHE_SUBJECT_SIZE];

    CERTCACHE_ENTER_FN();

    if (certcache == NULL || cert == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* Determine which hash table chain we'll be working with */
    get_cert_subject(cert, subject);
    index = PINT_certcache_get_index(subject);

    /* Remove expired certificates in this chain */
    PINT_certcache_rm_expired_entries(0, index);

    entry = (struct certcache_entry_s *) malloc(sizeof(struct certcache_entry_s));
    if (entry == NULL)
    {
        return -PVFS_ENOMEM;
    }

    ret = PINT_certcache_convert_certificate(cert, uid, num_groups, group_array, entry);
    if (ret != 0)
    {
        ret = -PVFS_ENOMEM;
        goto destroy_entry;
    }

    PINT_certcache_debug_certificate(cert, "Caching");

    /* acquire the lock */
    lock_lock(&lock);

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "entry %p added to the head of the "
                 "linked list @ index = %d\n", entry, index);
    if (PINT_llist_add_to_head(certcache->entries[index], entry) < 0)
    {
        ret = -PVFS_ENOMEM;
        goto destroy_entry;
    }

    /* unlock the certificate cache lock */
    lock_unlock(&lock);

    certcache->stats->inserts++;
    certcache->stats->entry_cnt++;

    CERTCACHE_EXIT_FN();

    return 0;

destroy_entry:
    free(entry);

    CERTCACHE_EXIT_FN();

    return ret;
}

/* Remove */
int PINT_certcache_remove_entry(struct certcache_entry_s *entry)
{
    int ret = 0;
    uint16_t index = 0;
    struct certcache_entry_s *rem;

    CERTCACHE_ENTER_FN();

    /* lock certificate cache */
    lock_lock(&lock);
        
    index = PINT_certcache_get_index(entry->subject);
    rem = (struct certcache_entry_s *) 
              PINT_llist_rem(certcache->entries[index],
                             entry->subject,
                             &PINT_certcache_subject_cmp);
    
    /* unlock certificate cache */
    lock_unlock(&lock);

    /* free memory */
    if (rem != NULL)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "removed entry %p at "
                     "index %hd\n", rem, index);
        free(rem);
        certcache->stats->removed++;
        certcache->stats->entry_cnt--;
    }

    CERTCACHE_EXIT_FN();
    return ret;
}

/*****************************************************************************/
/** PINT_certcache_get_index
 * Determine the hash table index based on certian PVFS_certificate fields.
 * Returns index of hash table.
 */
static uint16_t PINT_certcache_get_index(const char *subject)
{    
    uint32_t seed = 42; //Seed Murmur3
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;

    /* Hash certificate subject */
    MurmurHash3_x64_128((const void *) subject,
        strlen(subject), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    index = (uint16_t) (hash1[0] % CERTCACHE_HASH_MAX);

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tindex=%hu\n", index);

    return index;
}

/** PINT_certcache_certificate_cmp
 * Compares two PVFS_certificate structures. Returns 0 if the
 * certificates are equivalent. Returns nonzero otherwise.
 */
static int PINT_certcache_certificate_cmp(void *cert, void *entry)
{
    PVFS_certificate *kcert;
    struct certcache_entry_s *pentry;
    char subject[CERTCACHE_SUBJECT_SIZE];

    kcert = (PVFS_certificate *) cert;
    pentry = (struct certcache_entry_s *) entry;

    /* ignore chain end marker */
    if (pentry->subject[0] == '\0')
    {
        return 1;
    }

    get_cert_subject(kcert, subject);

    /* compare signatures */
    return strcmp(subject, pentry->subject);
}

/** PINT_certcache_subject_cmp
 *  Compares certificate subject string w/entry subject
 */ 
static int PINT_certcache_subject_cmp(void *subject, 
    void *entry)
{
    return strcmp((const char *) subject, 
                  ((struct certcache_entry_s *) entry)->subject);
}

/** PINT_certcache_reset_stats
 * Resets the certificate cache statistics. This function is not
 * MT/MP safe.
 */
static void PINT_certcache_reset_stats(struct certcache_stats_s const *stats)
{
    memset((void *) stats, 0, sizeof(struct certcache_stats_s));
}

/** PINT_certcache_convert_certificate
 * Converts a certificate to a certificate cache entry.
 * Returns 0 on success.
 * Return negative PVFS error on failure.
 */
static int PINT_certcache_convert_certificate(
    const PVFS_certificate * cert,
    PVFS_uid uid,
    uint32_t num_groups,
    PVFS_gid *group_array,
    struct certcache_entry_s *entry)
{
    char subject[CERTCACHE_SUBJECT_SIZE];

    if (entry == NULL || cert == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* note: may be 0 if cert close to expiration */
    entry->expiration = get_expiration(cert);

    /* copy subject */
    get_cert_subject(cert, subject);

    strncpy(entry->subject, subject, sizeof(subject));

    /* copy other fields */
    entry->uid = uid;
    entry->num_groups = num_groups;
    /* allocate group array -- free in cleanup */
    entry->group_array = (PVFS_gid *) malloc(num_groups * sizeof(PVFS_gid));
    if (entry->group_array == NULL)
    {
        return -PVFS_ENOMEM;
    }
    memcpy(entry->group_array, group_array, num_groups * sizeof(PVFS_gid));

    return 0;
}

/** PINT_certcache_cleanup_entry()
 *  Frees allocated members of certcache_entry_s and then frees
 *  the entry.
 */
static void PINT_certcache_cleanup_entry(void * entry)
{
    if (entry != NULL)
    {
        if (((struct certcache_entry_s *) entry)->group_array != NULL)
        {
            free(((struct certcache_entry_s *) entry)->group_array);
            ((struct certcache_entry_s *) entry)->group_array = NULL;
        }
        free(entry);
    }
}

/* LOCKING */
/** lock_init() 
 * Initializes the proper lock based on the LOCK_TYPE
 * Returns 0 on success, -1 on error
 */
static int lock_init(certcache_lock_t * lock)
{
    int ret = -1;

    /* TODO: ability to disable locking */
#if LOCK_TYPE == 0
    return 0;
#elif LOCK_TYPE == 1
    pthread_mutexattr_t attr;
    ret = pthread_mutexattr_init(&attr);
    if (ret != 0) return -1;
    ret = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (ret != 0) return -1;
    ret = pthread_mutex_init(lock, &attr);
    if (ret != 0) return -1;
#elif LOCK_TYPE == 2
    ret = pthread_spin_init(lock, 1);
    if (ret != 0) return -1;
#elif LOCK_TYPE == 3
    *lock = (certcache_lock_t) GEN_SHARED_MUTEX_INITIALIZER_NP;
    ret = 0;
#endif
    return ret;
}

/** lock_lock()
 * Acquires the lock referenced by lock and returns 0 on
 * Success; otherwise, returns -1 and sets errno.
 */
static inline int lock_lock(certcache_lock_t * lock)
{
    int ret = 0;
    
#if LOCK_TYPE == 0
    return ret;
#elif LOCK_TYPE == 1
    ret = pthread_mutex_lock(lock);
    return ret;
#elif LOCK_TYPE == 2
    return pthread_spin_lock(lock);
#elif LOCK_TYPE == 3
    ret = gen_mutex_lock(lock);
    return ret;
#endif
}

/** lock_unlock()
 * Unlocks the lock.
 * If successful, return zero; otherwise, return -1 and sets errno.
 */
static inline int lock_unlock(certcache_lock_t * lock)
{
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

/** lock_trylock
 * Tries the lock to see if it's available:
 * Returns 0 if lock has not been acquired ie: success
 * Otherwise, returns -1
 */
static inline int lock_trylock(certcache_lock_t * lock)
{
    int ret = -1;

#if (LOCK_TYPE == 0)
    return 0;
#elif (LOCK_TYPE == 1)
    ret = pthread_mutex_trylock(lock);
    if (ret != 0)
    {
        ret = -1;
    }
#elif (LOCK_TYPE == 2)
    ret = pthread_spin_trylock(lock);
    if (ret != 0)
    {
        ret = -1;
    }
#elif LOCK_TYPE == 3
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
#endif /* ENABLE_CERTCACHE */
