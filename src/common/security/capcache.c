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
static int PINT_capcache_compare_by_cap(void *data,
                                        void *entry);
static int PINT_capcache_compare_by_id(void *cap_id,
                                       void *entry);
/*
static int PINT_capcache_compare_by_handle(void *handle,
                                           void *entry);
*/
static void PINT_capcache_cleanup(void *entry);
static void PINT_capcache_debug(const char *prefix,
                                void *data);

/* method table */
static seccache_methods_t capcache_methods = {    
    PINT_seccache_expired_default,
    PINT_capcache_set_expired,
    PINT_capcache_get_index,
    PINT_capcache_compare_by_cap,
    PINT_capcache_cleanup,
    PINT_capcache_debug
};

/* internal list with cap IDs and handles */
/*typedef struct capcache_id_entry_s {
    PVFS_capability_id cap_id;
    uint32_t num_handles;
    PVFS_handle *handle_array;
} capcache_id_entry_t;
*/
/*                                                              
#ifdef ENABLE_REVOCATION

static int capcache_id_list_insert(capcache_data_t *data);

static int capcache_id_list_remove(capcache_data_t *data);

static void capcache_id_list_free(void *data);

static int capcache_id_list_id_cmp(void *key,
                                   void *data);

static capcache_data_t *capcache_id_list_lookup_by_id(
    PVFS_capability_id cap_id);

static int capcache_id_list_handle_cmp(void *key,
                                       void *data);

static capcache_data_t *capcache_id_list_lookup_by_handle(
    PVFS_handle handle);

PINT_llist_p capcache_id_list;

#endif
*/

/* entry - seccache_entry_t struct */
#define CAPCACHE_ENTRY_CAP(entry)    (((capcache_data_t *) entry->data)->cap)

/* data - data field from seccache_entry_t */
#define CAPCACHE_DATA_CAP(data)      (((capcache_data_t *) data)->cap)

/*** capability cache helper functions ***/
#ifdef ENABLE_SECURITY_MODE
/* TODO -r remove static int PINT_capcache_compare(void *key,
                              void *entry); */
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
    /* TODO remove? PVFS_capability *cap = CAPCACHE_DATA_CAP(data); */
    capcache_data_t *int_data = (capcache_data_t *) data;
    uint32_t seed = 42;
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;

    /* Only the first (metadata) handle is hashed - we use the handle
       to search the cache */
    MurmurHash3_x64_128((const void *) &int_data->handle,
        sizeof(PVFS_handle), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    index = (uint16_t) (hash1[0] % hash_limit);

    return index;
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

/* TODO: needed? 
#ifdef ENABLE_REVOCATION
            capcache_id_list_remove((capcache_data_t *) pentry->data);
#endif
*/
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
    char sig_buf[16], mask_buf[16];
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

static int PINT_capcache_compare_by_cap(void *data,
                                        void *entry)
{
    PVFS_capability *ecap, *kcap;
    seccache_entry_t *pentry;

    pentry = (seccache_entry_t *) entry;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecap = CAPCACHE_ENTRY_CAP(pentry);
    kcap = CAPCACHE_DATA_CAP(data);

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: comparing cap %llx to cap %llx\n",
                 __func__, llu(kcap->cap_id), llu(ecap->cap_id));

    return ecap->cap_id != kcap->cap_id;
}

int PINT_capcache_lookup_by_cap(PVFS_capability *cap,
                                int *cap_flags)
{
    seccache_entry_t *int_entry, *rev_entry;
    capcache_data_t cmp_data, *data;
    int ret, int_flags;

#ifdef ENABLE_REVOCATION
    /* check revocation list for revoked cap */
    rev_entry = PINT_revlist_lookup(cap->cap_id);
    if (rev_entry != NULL)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: located revocation "
                     "list entry for cap %llx\n", __func__,
                     llu(cap->cap_id));
        if (!PINT_capability_is_null(cap))
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: inserting revoked "
                         "cap %llx from revocation list\n", __func__,
                         llu(cap->cap_id));
            /* cache capability with revoked flag */
            ret = PINT_capcache_insert(cap, CAPCACHE_REVOKED);
            if (ret == 0)
            {    
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
            
            int_flags = CAPCACHE_REVOKED;
        }
    }
    else
    {
#endif /* ENABLE_REVOCATION */
        /* look up capability in cache */
        cmp_data.cap_id = cap->cap_id;
        cmp_data.cap = cap;
        cmp_data.flags = 0;
        cmp_data.handle = cap->handle_array[0];

        int_entry = PINT_seccache_lookup(capcache, &cmp_data);
        if (int_entry != NULL)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: cap %llx found\n", __func__,
                         llu(cap->cap_id));

            data = (capcache_data_t *) int_entry->data;
            int_flags = data->flags;
        }
        else
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: cap %llx not found\n",
                          __func__, llu(cap->cap_id));
        }
#ifdef ENABLE_REVOCATION
    }
#endif

    /* determine return code */
    if (int_flags & CAPCACHE_REVOKED)
    {
        ret = CAPCACHE_LOOKUP_REVOKED;
    }
    else if (int_entry != NULL)
    {
        ret = CAPCACHE_LOOKUP_FOUND;
    }
    else
    {
        ret = CAPCACHE_LOOKUP_NOT_FOUND;
    }

    if (cap_flags != NULL)
    {
        *cap_flags = int_flags;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: returning %d\n", __func__, ret);

    return ret;
}

/** PINT_capcache_compare_by_id
 *  Compares the supplied id to the id of the a capability.
 *  Returns 0 if equal, nonzero otherwise.
 */
static int PINT_capcache_compare_by_id(void *data,
                                       void *entry)
{
    capcache_data_t *int_data = (capcache_data_t *) data;
    PVFS_capability *ecap;
    seccache_entry_t *pentry;

    pentry = (seccache_entry_t *) entry;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecap = CAPCACHE_ENTRY_CAP(pentry);

    return ecap->cap_id != int_data->cap_id;
}

/* PINT_capcache_lookup_by_id
 * lookup entry using capability id
 *    returns:
 *       CAPCACHE_LOOKUP_NOT_FOUND (0)
 *       CAPCACHE_LOOKUP_FOUND (1)
 *       CAPCACHE_LOOKUP_REVOKED (2)
 *       or -PVFS_error on error
 */
int PINT_capcache_lookup_by_id(PVFS_capability_id cap_id,
                               PVFS_handle handle,
                               PVFS_capability **cap,
                               int *cap_flags)
{
    capcache_data_t cmp_data;
    seccache_entry_t *int_entry;
    int ret = 0, int_flags = 0;

    if (cap == NULL) {
        return -PVFS_EINVAL;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: looking up cap %llx\n", 
                 __func__, llu(cap_id));

    /* look up capability using cap id */
    cmp_data.cap_id = cap_id;
    cmp_data.cap = NULL;
    cmp_data.flags = 0;
    cmp_data.handle = handle;

    int_entry = PINT_seccache_lookup_cmp(capcache, PINT_capcache_compare_by_id, &cmp_data);
    if (int_entry != NULL)
    {
        int_flags = ((capcache_data_t *) int_entry->data)->flags;
    }

    /* determine return code */
    if (int_flags & CAPCACHE_REVOKED)
    {
        ret = CAPCACHE_LOOKUP_REVOKED;
        /* TODO: dup? */
        *cap = CAPCACHE_ENTRY_CAP(int_entry);
    }
    else if (int_entry != NULL)
    {
        ret = CAPCACHE_LOOKUP_FOUND;
        /* TODO: dup? */
        *cap = CAPCACHE_ENTRY_CAP(int_entry);
    }
    else
    {
        ret = CAPCACHE_LOOKUP_NOT_FOUND;
    }

    if (cap_flags != NULL)
    {
        *cap_flags = int_flags;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: returning %d\n", __func__, ret);

    return ret;
}

/** PINT_capcache_compare_by_handle
 * Compares the supplied handle to the metadata handle of a
 * capability. Returns 0 if equal, nonzero otherwise.
 */
/* TODO: needed? */
#if 0 
static int PINT_capcache_compare_by_handle(void *data,
                                           void *entry)
{
    capcache_data_t *int_data = (capcache_data_t *) data;
    PVFS_capability *ecap;
    seccache_entry_t *pentry;

    pentry = (seccache_entry_t *) entry;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecap = CAPCACHE_ENTRY_CAP(pentry);

    return ecap->handle_array[0] != int_data->handle;
}
#endif

/** PINT_capcache_lookup_by_handle
 *  Returns all capabilities for the specified handle.
 */
int PINT_capcache_lookup_by_handle(PVFS_handle handle,
                                   PVFS_capability **cap_array[],
                                   uint32_t *num_caps)
{
    capcache_data_t cmp_data, *data;
    uint16_t index;
    PINT_llist_p l_p, cap_list;
    seccache_entry_t *entry;
    PVFS_capability *cap, *dup_cap, **int_cap_array;
    int cap_i, ret;

    if (cap_array == NULL || num_caps == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* search data */
    cmp_data.cap = NULL;
    cmp_data.flags = 0;
    cmp_data.handle = handle;

    PINT_seccache_lock(capcache);

    /* get the index of the hash table chain */
    index = capcache->methods.get_index(&cmp_data, capcache->hash_limit);

    /* make linked list of capabilities that match the handle */
    cap_list = PINT_llist_new();
    if (cap_list == NULL)
    {
        PINT_seccache_unlock(capcache);

        return -PVFS_ENOMEM;
    }

    for (l_p = capcache->hash_table[index], *num_caps = 0; 
         l_p;
         l_p = PINT_llist_next(l_p))
    {
        entry = (seccache_entry_t *) l_p->item;
        /* end marker */
        if (entry == NULL || entry->data == NULL)
        {
            continue;
        }

        cap = CAPCACHE_ENTRY_CAP(entry);
        data = (capcache_data_t *) entry->data;
        if (handle == cap->handle_array[0] &&
            !(data->flags & CAPCACHE_REVOKED))
        {
            /* copy cap to new data structure */
            dup_cap = PINT_dup_capability(cap);
            if (dup_cap == NULL)
            {
                PINT_seccache_unlock(capcache);

                return -PVFS_ENOMEM;
            }

            ret = PINT_llist_add_to_tail(cap_list, dup_cap);
            if (ret != 0)
            {
                PINT_seccache_unlock(capcache);

                PINT_cleanup_capability(dup_cap);

                return -PVFS_ENOMEM;
            }

            (*num_caps)++;
        }
    }

    PINT_seccache_unlock(capcache);

    if (*num_caps == 0)
    {
        return 0;
    }

    int_cap_array = (PVFS_capability **) calloc(*num_caps, sizeof(PVFS_capability *));
    if (int_cap_array == NULL)
    {
        /* TODO: free cap_list */
        return -PVFS_ENOMEM;
    }

    for (l_p = cap_list->next, cap_i = 0; l_p; l_p = PINT_llist_next(l_p), cap_i++)
    {
        int_cap_array[cap_i] = (PVFS_capability *) l_p->item;
    }

    *cap_array = int_cap_array;

    /* TODO: free cap list */

    return 0;
}

/* PINT_capcache_free_cap_array
 * Frees caps allocated by PINT_capcache_lookup_by_handle
 */
void PINT_capcache_free_cap_array(PVFS_capability *cap_array[],
                                  uint32_t num_caps)
{
    uint32_t i;

    if (cap_array == NULL)
    {
        return;
    }

    for (i = 0; i < num_caps; i++)
    {
        PINT_cleanup_capability(cap_array[i]);
    }

    free(cap_array);
}

/* insert entry for capability */
int PINT_capcache_insert(PVFS_capability *cap, int cap_flags)
{
    PVFS_capability *cachecap = NULL;
    capcache_data_t *data;
    int ret = 0;

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "Capability cache: inserting cap "
                 "%llx\n", llu(cap->cap_id));

    /* copy the cap for the cache */
    data = (capcache_data_t *) calloc(1, sizeof(capcache_data_t));
    cachecap = PINT_dup_capability(cap);
    if (data == NULL || cachecap == NULL)
    {
        if (data != NULL)
        {
            free(data);
        }
        if (cachecap != NULL)
        {
            PINT_cleanup_capability(cachecap);
        }

        return -PVFS_ENOMEM;
    }

    data->cap = cachecap;
    data->flags = cap_flags;
    data->handle = cachecap->handle_array[0];
    data->cap_id = cachecap->cap_id;

    /* insert into the cache */
    ret = PINT_seccache_insert(capcache, data, sizeof(capcache_data_t));
    if (ret == 0) {
/* TODO: needed?
#ifdef ENABLE_REVOCATION
        int ret2 = capcache_id_list_insert(data);
            
        if (ret2 != 0)
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: Could not insert id "
                         "entry (%d)\n", __func__, ret2);
        }
#endif
*/
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "Capability cache: inserted "
                     "data %p (cap %llx [%p])\n", data, llu(cap->cap_id),
                     cap);

    }
    else
    {
        PVFS_perror_gossip("PINT_capcache_insert: insert failed", ret);
    }

    return ret;
}

#ifdef ENABLE_SECURITY_MODE

static int PINT_capcache_compare_quick(void *data,
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
    kcap = CAPCACHE_DATA_CAP(data);

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
int PINT_capcache_quick_sign(PVFS_capability *cap)
{
    seccache_entry_t *curr_entry = NULL;
    capcache_data_t cmp_data;
    uint16_t index = 0;
    PVFS_capability *curr_cap;

    /* fill in data for comparison */
    cmp_data.cap = cap;
    cmp_data.flags = 0;
    cmp_data.handle = cap->handle_array[0];

    PINT_seccache_lock(capcache);

    /* get the index of the hash table chain */
    index = capcache->methods.get_index(&cmp_data, capcache->hash_limit);

    /* iterate over the hash table chain at the calculated index until a match
     * is found.
     */
    curr_entry = (seccache_entry_t *) PINT_llist_search(
        capcache->hash_table[index],
        &cmp_data,
        &PINT_capcache_compare_quick);

    /* release the lock */
    PINT_seccache_unlock(capcache);

    if (curr_entry != NULL)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: entry found\n",
                     __func__);
    }
    else
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: no entry\n",
                     __func__);
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
#endif /* ENABLE_SECURITY_MODE */

#ifdef ENABLE_REVOCATION

/* revoke a capability or create an entry on the revocation list */
int PINT_capcache_revoke_cap(PVFS_handle handle,
                             PVFS_capability_id cap_id)
{
    int ret = 0, cap_flags;
    seccache_entry_t entry;
    capcache_data_t data;
    PVFS_capability *cap = NULL, *copy_cap = NULL;

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: revoking cap %llx\n", 
                 __func__, llu(cap_id));

    /* lookup cap on id list */
    /* TODO: remove
    PINT_seccache_lock(capcache);

    data = capcache_id_list_lookup_by_id(cap_id);

    PINT_seccache_unlock(capcache);
    */

    ret = PINT_capcache_lookup_by_id(cap_id, handle, &cap, &cap_flags);

    if (ret == CAPCACHE_LOOKUP_REVOKED)
    {
        /* already revoked -- done */
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: cap already revoked\n",
                     __func__);
        return 0;
    }
    else if (ret == CAPCACHE_LOOKUP_FOUND)
    {
        /* set up an entry to use for PINT_seccache_remove */
        data.cap = cap;
        data.flags = cap_flags;
        data.handle = cap->handle_array[0];

        memset(&entry, 0, sizeof(seccache_entry_t));
        entry.data = &data;
        entry.data_size = sizeof(capcache_data_t);

        /* copy the capability */
        copy_cap = PINT_dup_capability(cap);
        if (copy_cap == NULL)
        {
            return -PVFS_ENOMEM;
        }
        
        /* remove current capability entry */
        ret = PINT_seccache_remove(capcache, &entry);
        if (ret == 0)
        {
            /* remove valid and set revoked flag */
            cap_flags &= ~CAPCACHE_VALID;
            cap_flags |= CAPCACHE_REVOKED;

            /* insert revoked capability */
            ret = PINT_capcache_insert(copy_cap, CAPCACHE_REVOKED);
        }
        else
        {
            gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: error: could not "
                         "remove cap id %llx (%d)\n", __func__, 
                         llu(cap_id), ret);
        }

        /* cap has been copied (again), so cleanup our copy */
        PINT_cleanup_capability(copy_cap);
    }
    else if (ret == CAPCACHE_LOOKUP_NOT_FOUND)
    {
        /* store cap_id in revocation list */
        ret = PINT_revlist_insert(cap_id);
    }
    else
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: error %d revoking cap %llx\n", 
                     __func__, ret, llu(cap_id));
    }

    return ret;
}

#endif /* ENABLE_REVOCATION */

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

/*
#ifdef ENABLE_REVOCATION
    capcache_id_list = PINT_llist_new();
    if (capcache_id_list == NULL)
    {
        return -PVFS_ENOMEM;
    }
#endif
*/
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
/*
#ifdef ENABLE_REVOCATION
    PINT_llist_free(capcache_id_list, capcache_id_list_free);
#endif
*/
    return 0;
}


/* TODO: not used? */
#if 0

/* #ifdef ENABLE_REVOCATION */

static int capcache_id_list_insert(capcache_data_t *data)
{   
    return PINT_llist_add_to_head(capcache_id_list, data);
}

static int capcache_id_list_remove(capcache_data_t *data)
{
    PINT_llist_rem(capcache_id_list, &data->cap->cap_id, 
                   capcache_id_list_id_cmp);

    return 0;
}

/* freeing the data is handled by the capcache itself */
static void capcache_id_list_free(void *data)
{
    return;
}

/* check whether capability matches id */
static int capcache_id_list_id_cmp(void *key,
                                   void *data)
{
    PVFS_capability_id cap_id = *((PVFS_capability_id *) key);
    capcache_data_t *pdata = (capcache_data_t *) data;
    
    return cap_id != pdata->cap->cap_id;
}

static capcache_data_t *capcache_id_list_lookup_by_id(
    PVFS_capability_id cap_id)
{
    return (capcache_data_t *) PINT_llist_search(capcache_id_list,
                                                 &cap_id, 
                                                 capcache_id_list_id_cmp);
}

/** capcache_handle_cmp
 *  Determine whether handle is covered by capability
 */
static int capcache_id_list_handle_cmp(void *key, 
                                       void *data)
{
    PVFS_handle handle = *((PVFS_handle *) key);
    capcache_data_t *pdata = (capcache_data_t *) data;
    int i;
   
    for (i = 0; i < pdata->cap->num_handles; i++)
    {
        if (pdata->cap->handle_array[i] == handle)
        {
            return 0;
        }
    }

    return 1;
}

static capcache_data_t *capcache_id_list_lookup_by_handle(
    PVFS_handle handle)
{
    return (capcache_data_t *) PINT_llist_search(capcache_id_list, &handle,
                                   capcache_id_list_handle_cmp);
}

/* find capability given handle */
int PINT_capcache_lookup_by_handle(PVFS_handle handle,
                                   PVFS_capability **cap_array,
                                   uint64_t *num_handles)
{
    capcache_data_t *data;

    if (cap == NULL)
    {
        return -PVFS_EINVAL;
    }

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: looking up handle %llu\n",
                 __func__, llu(handle));

    PINT_seccache_lock(capcache);

    data = capcache_id_list_lookup_by_handle(handle);
    
    PINT_seccache_unlock(capcache);

    if (data != NULL)
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: handle found - flags %d\n",
                     __func__, data->flags);

        *cap = data->cap;
    }
    else
    {
        gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s: handle not found\n",
                     __func__);

        return CAPCACHE_LOOKUP_NOT_FOUND;
    }

    return (data->flags & CAPCACHE_REVOKED) ? CAPCACHE_LOOKUP_REVOKED : 
            CAPCACHE_LOOKUP_FOUND;
}

/* #endif ENABLE_REVOCATION */

#endif /* 0 */

#endif /* ENABLE_CAPCACHE */
