/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Capability cache functions
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_REVOCATION

#include <malloc.h>
#include <string.h>

#include "revlist.h"
#include "server-config.h"
#include "security-util.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "murmur3.h"
#include "gossip.h"
#include "pvfs2-debug.h"

/* global variables */
seccache_t *revlist = NULL;

/* revocation list declarations */
#define REVLIST_ENTRY_DATA(entry) \
    ((revocation_data_t *) ((seccache_entry_t *) entry)->data)

#define REVLIST_DATA(data)    ((revocation_data_t *) data)

#define REVLIST_TIMEOUT    10

/* revocation list methods */
static void PINT_revlist_set_expired(seccache_entry_t *entry,
                                      PVFS_time timeout);
static uint16_t PINT_revlist_get_index(void *data,
                                       uint64_t hash_limit);
static int PINT_revlist_compare(void *data, 
                                void *entry);
static void PINT_revlist_cleanup(void *entry);
static void PINT_revlist_debug(const char *prefix,
                               void *data);

/* method table */
static seccache_methods_t revlist_methods = {    
    PINT_seccache_expired_default,
    PINT_revlist_set_expired,
    PINT_revlist_get_index,
    PINT_revlist_compare,
    PINT_revlist_cleanup,
    PINT_revlist_debug
};

/** PINT_revlist_set_expired
 * set the entry expiration to the data expiration (timeout not
 * used)
 */
static void PINT_revlist_set_expired(seccache_entry_t *entry,
                                     PVFS_time timeout)
{
    revocation_data_t *rev_data = REVLIST_ENTRY_DATA(entry);

    entry->expiration = rev_data->expiration;
}

/** PINT_revlist_get_index
 * Determine the hash table index based on the revocation entry
 * fields.
 * 
 * Returns index of hash table.
 */
static uint16_t PINT_revlist_get_index(void *data,
                                       uint64_t hash_limit)
{    
    revocation_data_t *rev_data = REVLIST_DATA(data);
    uint32_t seed = 42;
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;

    /* What to hash:
        server
        cap_id
    */
	if (rev_data->server != NULL)
	{
        MurmurHash3_x64_128((const void *) rev_data->server,
            strlen(rev_data->server), seed, hash2);
        hash1[0] += hash2[0];
        hash1[1] += hash2[1];
	}

    MurmurHash3_x64_128((const void *) &rev_data->cap_id,
        sizeof(PVFS_capability_id), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    index = (uint16_t) (hash1[0] % hash_limit);

    return index;
}

/**
 * PINT_revlist_compare
 * 
 * check if capability is in revocation list, based on id
 * 
 * return 0 on match, nonzero otherwise
 */
int PINT_revlist_compare(void *key,
                         void *entry)
{
	seccache_entry_t *pentry = SECCACHE_ENTRY(entry);
    revocation_data_t *key_data, *list_data;

	/* ignore chain end marker */
	if (pentry->data == NULL)
	{
		return 1;
	}

	key_data = REVLIST_DATA(key);
	list_data = REVLIST_ENTRY_DATA(entry);

    return (key_data->cap_id != list_data->cap_id);
}

/**
 * PINT_revlist_free
 * 
 * free data memory
 */
void PINT_revlist_cleanup(void *entry)
{
    revocation_data_t *rev_data = REVLIST_ENTRY_DATA(entry);
    if (rev_data != NULL)
    {
        if (rev_data->server != NULL)
        {
            free(rev_data->server);
        }

        free(rev_data);
    }
}

/* PINT_revlist_debug
 *
 * Outputs the fields of revocation data.
 * prefix should typically be "caching" or "removing".
 */
static void PINT_revlist_debug(const char *prefix,
                               void *data)
{
    revocation_data_t *rev_data = REVLIST_DATA(data);

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s revocation data:\n", prefix);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tserver: %s\n", 
                 rev_data->server ? rev_data->server : "(none)");
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tid: %llu\n", llu(rev_data->cap_id));

}

/** PINT_revlist_init
 * Initializes the revocation list.
 * Returns 0 on success; returns -PVFS_error on failure
 */
int PINT_revlist_init(void)
{
    gossip_debug(GOSSIP_SECURITY_DEBUG, "Initializing revocation list...\n");

    revlist = PINT_seccache_new("Revocation", &revlist_methods, 0);
    if (revlist == NULL)
    {
        return -PVFS_ENOMEM;
    }

    PINT_seccache_set(revlist, SECCACHE_TIMEOUT, REVLIST_TIMEOUT);

    return 0;
}

/** Releases resources used by the revocation list.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_revlist_finalize(void)
{

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Finalizing revocation list...\n");    

    PINT_seccache_cleanup(revlist);

    return 0;
}

/**
 * PINT_revlist_lookup
 * 
 * search for revocation list data that matches id
 * 
 * return seccache_entry_t if found, NULL otherwise
 */
seccache_entry_t *PINT_revlist_lookup(const char *server,
									  PVFS_capability_id cap_id)
{
    revocation_data_t cmp_data;

    /* set up comparison entry */
    cmp_data.server = (char *) server;
    cmp_data.cap_id = cap_id;

    return PINT_seccache_lookup(revlist, &cmp_data);
}

/** PINT_revlist_insert
 * 
 * insert revocation list data
 * 
 * return 0 on success, -PVFS_ERROR otherwise
 */
int PINT_revlist_insert(const char *server, 
                        PVFS_capability_id cap_id)
{
    revocation_data_t *rev_data;
    int ret;

    if (server == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* allocate data and fields */
    rev_data = (revocation_data_t *) malloc(sizeof(revocation_data_t));
    if (rev_data == NULL)
    {
        return -PVFS_ENOMEM;
    }

    rev_data->server = (char *) malloc(strlen(server) + 1);
    if (rev_data->server == NULL)
    {
        free(rev_data);

        return -PVFS_ENOMEM;
    }

    /* copy fields */
    strcpy(rev_data->server, server);
    rev_data->cap_id = cap_id;
    rev_data->expiration = PINT_util_get_current_time() + 
        PINT_seccache_get(revlist, SECCACHE_TIMEOUT);

    /* add to revlist */
    ret = PINT_seccache_insert(revlist, rev_data, sizeof(revocation_data_t));

    if (ret != 0)
    {
        PVFS_perror_gossip("PINT_revlist_insert: insert failed", ret);
    }

    return ret;
}

/** PINT_revlist_remove
 * 
 *  remove entry
 * 
 *  returns 0 on success; -PVFS_error otherwise
 */
int PINT_revlist_remove(seccache_entry_t *entry)
{
    return PINT_seccache_remove(revlist, entry);
}

#endif /* ENABLE_REVOCATION */
