/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Credential cache functions
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CREDCACHE

#include <malloc.h>
#include <string.h>

#include "credcache.h"
#include "security-util.h"
#include "server-config.h"
#include "pint-util.h"
#include "murmur3.h"
#include "gossip.h"
#include "pvfs2-debug.h"

/* error-checking macros */
#define CHECK_RET(__err)    if ((__err)) return (__err)

#define CHECK_NULL_RET_NULL(__val)    if ((__val) == NULL) return NULL
                                           
/* global variables */
seccache_t * credcache = NULL;

/* credential cache methods */
static void PINT_credcache_set_expired(seccache_entry_t *entry,
                                       PVFS_time timeout);
static uint16_t PINT_credcache_get_index(void *data,
                                         uint64_t hash_limit);
static int PINT_credcache_compare(void *data, 
                                  void *entry);
static void PINT_credcache_cleanup(void *entry);
static void PINT_credcache_debug(const char * prefix, 
                                 void *data);

/* credential cache helper functions */

static seccache_methods_t credcache_methods = {
    PINT_seccache_expired_default,
    PINT_credcache_set_expired,
    PINT_credcache_get_index,
    PINT_credcache_compare,
    PINT_credcache_cleanup,
    PINT_credcache_debug
};
/* END of internal-only credential cache functions */

/* PINT_credcache_set_expired 
 * Set expiration of entry based on credential expiration stored in 
 * credcache data
 */
static void PINT_credcache_set_expired(seccache_entry_t *entry,
                                       PVFS_time timeout)
{    
    PVFS_credential *cred = (PVFS_credential *) entry->data;

    /* set expiration to now + supplied timeout */
    entry->expiration = time(NULL) + timeout;

    /* do not set expiration past credential timeout */
    if (cred->timeout < entry->expiration)
    {
        entry->expiration = cred->timeout;
    }
}

/*****************************************************************************/
/** PINT_credcache_get_index
 * Determine the hash table index based on credential subject.
 * Returns index of hash table.
 */
static uint16_t PINT_credcache_get_index(void *data,
                                         uint64_t hash_limit)
{    
    uint32_t seed = 42;
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;
    PVFS_credential *cred = (PVFS_credential *) data;

    /*** Hash credential fields ***/
    MurmurHash3_x64_128((const void *) cred->issuer,
        strlen(cred->issuer), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    MurmurHash3_x64_128((const void *) cred->signature,
        cred->sig_size, seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    index = (uint16_t) (hash1[0] % hash_limit);

    return index;
}

/** PINT_credcache_compare
 * Compare credential subjects
 */
static int PINT_credcache_compare(void *data,
                                  void *entry)
{
    seccache_entry_t *pentry = (seccache_entry_t *) entry;
    PVFS_credential *kcred, *ecred;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    ecred = (PVFS_credential *) pentry->data;
    kcred = (PVFS_credential *) data;

    /* if both sig_sizes are 0, they're null credentials */
    if (kcred->sig_size == 0 && ecred->sig_size == 0)
    {
        return 0;
    }
    /* sizes don't match -- shouldn't happen */
    else if (kcred->sig_size != ecred->sig_size)
    {
        gossip_err("Warning: credential cache: signature size mismatch "
                   "(key: %d   entry: %d)\n", kcred->sig_size, ecred->sig_size);
        return 1;
    }

    /* compare signatures */
    return memcmp(kcred->signature, ecred->signature, kcred->sig_size);
}

/** PINT_credcache_cleanup()
 *  Frees credential
 */
static void PINT_credcache_cleanup(void *entry)
{
    if (entry != NULL)
    {
        if (((seccache_entry_t *)entry)->data != NULL)
        {
            PINT_cleanup_credential((PVFS_credential *) 
                                    ((seccache_entry_t *)entry)->data);
            free(((seccache_entry_t *)entry)->data);
        }
        free(entry);
        entry = NULL;
    }
}

/* PINT_credcache_debug
 *
 * Outputs the subject of a credential.
 * prefix should typically be "caching" or "removing".
 */
static void PINT_credcache_debug(const char * prefix,
                                 void *data)
{
    PVFS_credential *cred = (PVFS_credential *) data;
    int i, buf_left, count;
    char group_buf[512], temp_buf[16];

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s credential:\n", prefix);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tissuer: %s\n", cred->issuer);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tuserid: %u\n", cred->userid);
    /* output groups */
    for (i = 0, group_buf[0] = '\0', buf_left = 512; i < cred->num_groups; i++)
    {
        count = sprintf(temp_buf, "%u ", cred->group_array[i]);
        if (count > buf_left)
        {
            break;
        }
        strcat(group_buf, temp_buf);
        buf_left -= count;
    }
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tgroups: %s\n", group_buf);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tsig_size: %u\n", cred->sig_size);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tsignature: %s\n",
                 PINT_util_bytes2str(cred->signature, temp_buf, 4));
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\ttimeout: %d\n", 
                 (int) cred->timeout);
#ifdef ENABLE_SECURITY_CERT
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tcertificate.buf_size: %u\n",
                 cred->certificate.buf_size);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tcertificate.buf: %s\n",
                 PINT_util_bytes2str(cred->certificate.buf, temp_buf, 4));
#endif
}

/** Initializes the credential cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_credcache_init(void)
{
    struct server_configuration_s *config = PINT_get_server_config();

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Initializing credential cache...\n");

    credcache = PINT_seccache_new("Credential", &credcache_methods, 0);
    if (credcache == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* Set timeout */
    PINT_seccache_set(credcache, SECCACHE_TIMEOUT, config->credcache_timeout);

    return 0;
}


/** Releases resources used by the credential cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_credcache_finalize(void)
{
    gossip_debug(GOSSIP_SECURITY_DEBUG, "Finalizing credential cache...\n");

    PINT_seccache_cleanup(credcache);

    return 0;
}


/** Lookup
 * Returns pointer to credential cache entry on success.
 * Returns NULL on failure.
 */
seccache_entry_t * PINT_credcache_lookup(PVFS_credential *cred)
{
    return PINT_seccache_lookup(credcache, cred);
}

/** PINT_credcache_insert
 * Inserts 'cred' into the credential cache.
 * Returns 0 on success; otherwise returns -PVFS_error.
 */
int PINT_credcache_insert(const PVFS_credential *cred)
{
    PVFS_credential *cachecred;
    int ret = 0;

    cachecred = PINT_dup_credential(cred);
    if (cachecred == NULL)
    {
        ret = -PVFS_ENOMEM;
    }

    if (ret == 0)
    {
        ret = PINT_seccache_insert(credcache, cachecred, 
                                   sizeof(PVFS_credential));
    }

    if (ret != 0)
    {
        PVFS_perror_gossip("PINT_credcache_insert: insert failed", ret);
    }

    return ret;
}

#endif /* ENABLE_CREDCACHE */
