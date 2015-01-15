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
#include "security-util.h"
#include "server-config.h"
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
static int PINT_capcache_compare(void *data, void *entry);
static void PINT_capcache_cleanup(void *entry);
static void PINT_capcache_debug(const char *prefix,
                               void *data);
int PINT_capcache_remove(seccache_entry_t * entry);

/* method table */
static seccache_methods_t capcache_methods = {    
    PINT_seccache_expired_default,
    PINT_capcache_set_expired,
    PINT_capcache_get_index,
    PINT_capcache_compare,
    PINT_capcache_cleanup,
    PINT_capcache_debug
};

/*** capability cache helper functions ***/
static int PINT_capcache_quick_cmp(void * data,
                                   void * entry);

/*** capability cache methods ***/

/** PINT_capcache_setexpired
 *  Sets the capcache entry's timeout to "now"
 */
static void PINT_capcache_set_expired(seccache_entry_t *entry,
                                      PVFS_time timeout)
{
    
    PVFS_capability *cap = (PVFS_capability *) entry->data;

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
    PVFS_capability *cap = (PVFS_capability *) data;
    uint32_t seed = 42;
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

    index = (uint16_t) (hash1[0] % hash_limit);

    return index;
}

/** PINT_capcache_capability_cmp
 * Compares two PVFS_capability structures. Returns 0 if the
 * capabilities are equivalent. Returns nonzero otherwise.
 */
static int PINT_capcache_compare(void * data,
                                 void * entry)
{
    seccache_entry_t *pentry = (seccache_entry_t *) entry;
    PVFS_capability *kcap, *ecap;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecap = (PVFS_capability *) pentry->data;
    kcap = (PVFS_capability *) data;

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

/** PINT_capcache_cleanup
 *  Frees allocated capability.
 */
static void PINT_capcache_cleanup(void *entry)
{
    if (entry != NULL)
    {
        if (((seccache_entry_t *) entry)->data != NULL)
        {
            PINT_cleanup_capability((PVFS_capability *) 
                                    ((seccache_entry_t *)entry)->data);
            free(((seccache_entry_t *)entry)->data);
        }
        free(entry);
        entry = NULL;
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
    char sig_buf[16], mask_buf[16];
    PVFS_capability *cap;
    int i;

    if (data == NULL)
    {
        return;
    }

    cap = (PVFS_capability *) data;

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

/** PINT_capcache_capability_cmp
 * Compares two PVFS_capability structures. Returns 0 if the
 * capability fields are equivalent. Returns nonzero otherwise.
 */
static int PINT_capcache_quick_cmp(void * data,
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

    ecap = pentry->data;
    kcap = (PVFS_capability *) data;

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
    uint16_t index = 0;
    PVFS_capability *curr_cap;

    index = capcache->methods.get_index(cap, capcache->hash_limit);

    PINT_seccache_lock(capcache);
    
    /* iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    curr_entry = (seccache_entry_t *) PINT_llist_search(
        capcache->hash_table[index],
        cap,
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
    curr_cap = (PVFS_capability *) curr_entry->data;
    /* check timeout */
    if (PINT_util_get_current_time() > curr_cap->timeout)
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


/** Initializes the capability cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_capcache_init(void)
{
    struct server_configuration_s *config = PINT_get_server_config();

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Initializing capability cache...\n");

    capcache = PINT_seccache_new("Capability", &capcache_methods, 0);
    if (capcache == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* Set timeout */
    PINT_seccache_set(capcache, SECCACHE_TIMEOUT, config->capcache_timeout);

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
seccache_entry_t * PINT_capcache_lookup(PVFS_capability * cap)
{
    return PINT_seccache_lookup(capcache, cap);
}

/* insert entry for capability */
int PINT_capcache_insert(PVFS_capability *cap)
{
    PVFS_capability *cachecap;
    int ret = 0;

    cachecap = PINT_dup_capability(cap);
    if (cachecap == NULL)
    {
        ret = -PVFS_ENOMEM;
    }

    if (ret == 0)
    {
        ret = PINT_seccache_insert(capcache, cachecap, 
                                   sizeof(PVFS_capability));
    }

    if (ret != 0)
    {
        PVFS_perror_gossip("PINT_certcache_insert: insert failed", ret);
    }

    return ret;
}

/* remove capability entry */
int PINT_capcache_remove(seccache_entry_t * entry)
{    
    return PINT_seccache_remove(capcache, entry);
}

#endif /* ENABLE_CAPCACHE */
