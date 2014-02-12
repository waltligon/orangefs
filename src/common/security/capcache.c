/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Capability cache functions
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CAPCACHE

#if defined(ENABLE_SECURITY_KEY) || defined(ENABLE_SECURITY_CERT)
#define ENABLE_SECURITY_MODE
#endif

#include <malloc.h>
#include <string.h>

#include "capcache.h"
#ifdef ENABLE_REVOCATION
#include "revlist.h"
#endif
#include "security-util.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "murmur3.h"
#include "gossip.h"
#include "pvfs2-debug.h"

/* global variables */
seccache_t *capcache = NULL;

/*** capability cache methods ***/
static void PINT_capcache_set_expired(seccache_entry_t *entry,
                                      PVFS_time timeout);
static uint16_t PINT_capcache_get_index(void *data,
                                        uint64_t hash_limit);
static int PINT_capcache_compare(void *key,
                                 void *entry);

static void PINT_capcache_cleanup(void *entry);
static void PINT_capcache_debug(const char *prefix,
                                void *data);

/* method table */
static seccache_methods_t capcache_methods = {    
    PINT_seccache_expired_default,
    PINT_capcache_set_expired,
    PINT_capcache_get_index,
    PINT_capcache_compare,
    PINT_capcache_cleanup,
    PINT_capcache_debug
};

/* entry - seccache_entry_t struct */
#define CAPCACHE_ENTRY_CAP(entry)    (((capcache_data_t *) entry->data)->cap)

/* data - data field from seccache_entry_t */
#define CAPCACHE_DATA_CAP(data)      (((capcache_data_t *) data)->cap)

/*** capability cache helper functions ***/
#ifdef ENABLE_SECURITY_MODE
static int PINT_capcache_quick_cmp(void *key,
                                   void *entry);
#endif

/*** capability cache methods ***/

/** PINT_capcache_set_expired
 *  Sets the capcache entry's timeout to "now" + timeout param
 */
static void PINT_capcache_set_expired(seccache_entry_t *entry,
                                      PVFS_time timeout)
{
    
    PVFS_capability *cap = CAPCACHE_ENTRY_CAP(entry);

    entry->expiration = time(NULL) + timeout;

    /* do not set expiration past capability timeout */
    if (cap->timeout < entry->expiration)
    {
        entry->expiration = cap->timeout;
    }
}

/** PINT_capcache_get_index
 * Determine the hash table index based on a certain key.
 * The key is a concatenation of serveral capability fields.
 * Returns index of hash table.
 */
static uint16_t PINT_capcache_get_index(void *data,
                                        uint64_t hash_limit)
{    
    PVFS_capability *cap = CAPCACHE_DATA_CAP(data);
    uint32_t seed = 42;
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;

    /* What to hash:
        issuer
        id
        fsid
        op_mask
        handle_array
    */
    MurmurHash3_x64_128((const void *) cap->issuer,
        strlen(cap->issuer), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    MurmurHash3_x64_128((const void *) &cap->cap_id,
        sizeof(uint64_t), seed, hash2);
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

    index = (uint16_t) (hash1[0] % hash_limit);

    return index;
}

/** PINT_capcache_compare
 * Compares two PVFS_capability structures. Returns 0 if the
 * capabilities are equivalent. Returns nonzero otherwise.
 */
static int PINT_capcache_compare(void *key,
                                 void *entry)
{
    seccache_entry_t *pentry = (seccache_entry_t *) entry;
    PVFS_capability *kcap, *ecap;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecap = CAPCACHE_ENTRY_CAP(pentry);
    kcap = CAPCACHE_DATA_CAP(key);

#if 0
    /* TODO: remove */
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
#endif

    return kcap->cap_id != ecap->cap_id;
}

/** PINT_capcache_cleanup
 *  Frees allocated capability and capcache entry.
 */
static void PINT_capcache_cleanup(void *entry)
{
    seccache_entry_t *pentry = (seccache_entry_t *) entry;
    PVFS_capability *cap;

    if (pentry != NULL && pentry->data != NULL)
    {
        cap = CAPCACHE_ENTRY_CAP(pentry);
        if (cap != NULL)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, 
                         "Capability cache: cleaning up data %p (cap %p)\n",
                         pentry->data, cap);

            PINT_cleanup_capability(cap);
        }

        free(pentry->data);
    }
}

/* PINT_capcache_debug
 *
 * Outputs the fields of a capability.
 * prefix should typically be "caching" or "removing".
 */
static void PINT_capcache_debug(const char *prefix,
                                void *data)
{
    char sig_buf[10], mask_buf[10];
    PVFS_capability *cap;
    int i;

    if (data == NULL)
    {
        return;
    }

    cap = CAPCACHE_DATA_CAP(data);

    if (strlen(cap->issuer) == 0)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s null capability\n", prefix);
        return;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s capability:\n", prefix);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tissuer: %s\n", cap->issuer);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tid: %llx\n", llu(cap->cap_id));
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

/** PINT_capcache_quick_cmp
 * Compares two PVFS_capability structures. Returns 0 if the
 * capability fields are equivalent. Returns nonzero otherwise.
 */
#ifdef ENABLE_SECURITY_MODE
static int PINT_capcache_quick_cmp(void *key,
                                   void *entry)
{
    PVFS_capability *kcap, *ecap;
    seccache_entry_t *pentry;

    pentry = (seccache_entry_t *) entry;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecap = CAPCACHE_ENTRY_CAP(pentry);
    kcap = CAPCACHE_DATA_CAP(key);

    return (!(kcap->fsid == ecap->fsid &&
              kcap->num_handles == ecap->num_handles &&
              kcap->op_mask == ecap->op_mask &&
              (strcmp(kcap->issuer, ecap->issuer) == 0) &&
              (memcmp(kcap->handle_array, ecap->handle_array, 
                      kcap->num_handles * sizeof(PVFS_handle)) == 0)));
}

/** PINT_capcache_quick_sign
 * Copy signature from cached capability if fields match
 */
int PINT_capcache_quick_sign(PVFS_capability * cap)
{
    seccache_entry_t *curr_entry = NULL;
    capcache_data_t cmp_data;
    uint16_t index = 0;
    PVFS_capability *curr_cap;

    /* fill in data for comparison */
    cmp_data.cap = cap;
    cmp_data.flags = 0;

    index = capcache->methods.get_index(&cmp_data, capcache->hash_limit);

    PINT_seccache_lock(capcache);
    
    /* iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    curr_entry = (seccache_entry_t *) PINT_llist_search(
        capcache->hash_table[index],
        &cmp_data,
        &PINT_capcache_quick_cmp);

    /* release the lock */
    PINT_seccache_unlock(capcache);

    if (curr_entry != NULL)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: entry found\n",
                     __func__);
    }
    else
    {
        return 1;
    }

    /* copy capability timeout & signature */
    curr_cap = CAPCACHE_ENTRY_CAP(curr_entry);
    /* check timeout */
    if (PINT_util_get_current_time() >= curr_cap->timeout)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: entry timed out\n",
                     __func__);
        return -1;
    }
    cap->timeout = curr_cap->timeout;
    cap->sig_size = curr_cap->sig_size;
    memcpy(cap->signature, curr_cap->signature, 
           curr_cap->sig_size);

    return 0;
}
#endif

/** PINT_capcache_id_cmp
 * Compares two PVFS_capability structures. Returns 0 if the
 * capability IDs are equal. Returns nonzero otherwise.
 */
#if 0

/* TODO: remove */

static int PINT_capcache_id_cmp(void * data,
                                void * entry)
{
    PVFS_capability *kcap, *ecap;
    seccache_entry_t *pentry;

    pentry = (seccache_entry_t *) entry;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecap = CAPCACHE_ENTRY_CAP(pentry);
    kcap = CAPCACHE_DATA_CAP(data);

    return ecap->cap_id != kcap->cap_id;
}

/** PINT_capcache_check_revocation
 *  Checks whether specified capability is revoked
 * 
 *  Returns 0 if not revoked (or not in cache)
 *  Returns CAPCACHE_CAP_REVOKED (1) if revoked
 *  Returns negative PVFS_error on error
 */ 
int PINT_capcache_check_revocation(PVFS_capability *cap)
{
    seccache_entry_t *curr_entry = NULL;
    capcache_data_t cmp_data, *cap_data;
    uint16_t index = 0;
    int ret, rev_flag = 0;

    /* TODO: last received revocation time comparison */

    /* fill in entry for comparison */
    cmp_entry.cap = cap;
    cmp_entry.flags = 0;

    index = capcache->methods.get_index(&cmp_data, capcache->hash_limit);

    PINT_seccache_lock(capcache);

    /* iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    curr_entry = (seccache_entry_t *) PINT_llist_search(
        capcache->hash_table[index],
        &cmp_data,
        &PINT_capcache_id_cmp);

    if (curr_entry == NULL)
    {
        /* check revocation list for match */
        ret = PINT_revlist_lookup(cap->issuer, cap->cap_id);
        if (ret == PINT_REVLIST_FOUND)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: revocation list entry "
                         "for cap %llu found\n", __func__, cap->cap_id);

            /* cap will be inserted into the cache with the revoked
               flag set */
            rev_flag = 1;

            /* remove revocation list entry */
            PINT_revlist_remove(cap->cap_id);
        }
        else if (ret < 0)
        {
            /* error */
            gossip_err("Warning: %s: revocation list search returned %d\n",
                         __func__, ret);
        }
    }
    else
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: entry for cap %llu found\n",
                     __func__, llu(cap->cap_id));
    }

    /* release the lock */
    PINT_seccache_unlock(capcache);

    if (rev_flag)
    {
        /* insert revoked capability */
        ret = PINT_capcache_insert(cap, CAPCACHE_REVOKED);
        if (ret < 0)
        {
            gossip_err("Warning: could not insert revoked capability:\n");
            PINT_debug_capability(cap, "Current");
        }
    }

    /* get revocation */
    if (curr_entry)
    {
        cap_data = (capcache_data_t *) curr_entry->data;
        return (cap_data->flags & CAPCACHE_REVOKED) ? 1 : 0;
    }
    else if (rev_flag)
    {
        return 1;
    }

    return 0;
}

#endif /* 0 */

/** PINT_capcache_init
 * Initializes the capability cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_init(void)
{

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Initializing capability cache...\n");

    capcache = PINT_seccache_new("Capability", &capcache_methods, 0);
    if (capcache == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* Set timeout */
    PINT_seccache_set(capcache, SECCACHE_TIMEOUT, CAPCACHE_TIMEOUT);

    return 0;
}

/** Releases resources used by the capability cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_finalize(void)
{

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Finalizing capability cache...\n");    

    PINT_seccache_cleanup(capcache);

    return 0;
}

/* lookup entry using capability */
seccache_entry_t * PINT_capcache_lookup(PVFS_capability * cap,
                                        int *cap_flags)
{
    capcache_data_t cmp_data;
    seccache_entry_t *entry, *rev_entry;
    int ret;

    cmp_data.cap = cap;
    cmp_data.flags = 0;

    entry = PINT_seccache_lookup(capcache, &cmp_data);

    if (entry == NULL)
    {
        /* check revocation list for revoked cap */
        rev_entry = PINT_revlist_lookup(cap->issuer, cap->cap_id);
        if (rev_entry != NULL)
        {
            /* cache capability with revoked flag */
            ret = PINT_capcache_insert(cap, CAPCACHE_REVOKED);
            if (ret == 0)
            {
                /* set entry to one just inserted */
                entry = PINT_seccache_lookup(capcache, &cmp_data);

                /* remove revocation list entry */
                ret = PINT_revlist_remove(rev_entry);
                if (ret != 0)
                {
                    PVFS_perror_gossip("Error removing revocation list entry", ret);
                }                
            }
            else
            {
                PVFS_perror_gossip("Error inserting revoked capability", ret);
            }
        }
    }

    if (entry != NULL && cap_flags != NULL)
    {
        *cap_flags = ((capcache_data_t *) entry->data)->flags;
    }

    return entry;
}

/* insert entry for capability */
int PINT_capcache_insert(PVFS_capability *cap, int flags)
{
    PVFS_capability *cachecap;
    capcache_data_t *data;
    int ret = 0;

    data = (capcache_data_t *) calloc(1, sizeof(capcache_data_t));
    cachecap = PINT_dup_capability(cap);
    if (data == NULL || cachecap == NULL)
    {
        if (data)
        {
            free(data);
        }
        if (cachecap)
        {
            PINT_cleanup_capability(cachecap);
        }

        ret = -PVFS_ENOMEM;
    }

    if (ret == 0)
    {
        data->cap = cachecap;
        data->flags = flags;

        ret = PINT_seccache_insert(capcache, data, sizeof(capcache_data_t));
        if (ret == 0) {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "Capability cache: inserted "
                         "data %p (cap %p)\n", data, cap);
        }
    }

    if (ret != 0)
    {
        PVFS_perror_gossip("PINT_capcache_insert: insert failed", ret);
    }

    return ret;
}

#endif /* ENABLE_CAPCACHE */
