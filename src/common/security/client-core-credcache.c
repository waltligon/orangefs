#include <stdio.h>
#include <stdlib.h>
#include <pvfs2-util.h>
#include <gossip.h>
#include <security-util.h>
#include <client-core-credcache.h>

credcache_options_t credcache_opts;
struct PINT_tcache *credential_cache;

/* TODO Do we need this? */
gen_mutex_t credcache_mutex = GEN_MUTEX_INITIALIZER;

/* calls the pvfs2-gencred app to generate a credential */
PVFS_credential *generate_credential(
    PVFS_uid uid,
    PVFS_gid gid)
{
    char user[16], group[16];
    int ret;
    PVFS_credential *credential;
    unsigned int timeout;

    ret = snprintf(user, sizeof(user), "%u", uid);
    if (ret < 0 || ret >= sizeof(user))
    {
        return NULL;
    }

    ret = snprintf(group, sizeof(group), "%u", gid);
    if (ret < 0 || ret >= sizeof(group))
    {
        return NULL;
    }

    credential = calloc(1, sizeof(*credential));
    if (!credential)
    {
        return NULL;
    }

    ret = PINT_tcache_get_info(credential_cache, TCACHE_TIMEOUT_MSECS,
                               &timeout);

    timeout = (ret != 0 || timeout == 0) ? PVFS2_DEFAULT_CREDENTIAL_TIMEOUT 
                : timeout/1000;

    ret = PVFS_util_gen_credential(
        user,
        group,
        timeout,
        credcache_opts.keypath,
        NULL,
        credential);
    if (ret < 0)
    {
        gossip_err("generate_credential: unable to generate credential\n");
        free(credential);
        return NULL;
    }

    return credential;
}

PVFS_credential *lookup_credential(
    PVFS_uid uid,
    PVFS_gid gid)
{
    struct credential_key ckey;
    struct credential_payload *cpayload;
    struct PINT_tcache_entry *entry;
    PVFS_credential *credential = NULL, *cache_cred = NULL, *copy_cred = NULL;
    struct timeval tval;
    int status;
    int ret;

    ckey.uid = uid;
    ckey.gid = gid;

    gen_posix_mutex_lock(&credcache_mutex);

    gossip_debug(GOSSIP_SECURITY_DEBUG, "credential cache lookup for (%u, %u)"
                 " num_entries: %d\n", uid, gid, credential_cache->num_entries);
    /* see if a fresh credential is in the cache */
    ret = PINT_tcache_lookup(credential_cache, &ckey, &entry, &status);
    if (ret == 0 && status == 0)
    {
        /* cache hit -- return copy of cached credential 
           (cache operations may free credential) */
        gossip_debug(GOSSIP_SECURITY_DEBUG,
                     "credential cache HIT for (%u, %u)\n", uid, gid);
        cpayload = (struct credential_payload*) entry->payload;
        /* TODO check return value */
        copy_cred = (PVFS_credential*) PINT_dup_credential(cpayload->credential);
        if (copy_cred == NULL)
        {
            gossip_lerr("out of memory\n");
        }
        gen_posix_mutex_unlock(&credcache_mutex);
        return copy_cred;
    }
    else if (ret == 0 && status == -PVFS_ETIME)
    {
        /* found expired cache entry -- remove */
        gossip_debug(GOSSIP_SECURITY_DEBUG, 
                     "deleting expired credential cache entry for (%u, %u)\n",
                     uid, gid);
        PINT_tcache_delete(credential_cache, entry);
    }

    /* request a new credential and store it in the cache */
    gossip_debug(GOSSIP_SECURITY_DEBUG,
                 "credential cache MISS for (%u, %u)\n", uid, gid);

    credential = generate_credential(uid, gid);
    if (credential == NULL)
    {
        gossip_err("unable to generate client credential for uid, gid "
                   "(%u, %u)\n", uid, gid);
        gen_posix_mutex_unlock(&credcache_mutex);
        return NULL;
    }

#ifdef ENABLE_SECURITY_CERT
    /* don't cache unsigned credential */
    if (credential->sig_size != 0)
    {
#endif
    cpayload = malloc(sizeof(struct credential_payload));
    if (cpayload == NULL)
    {
        gossip_lerr("out of memory\n");
        gen_posix_mutex_unlock(&credcache_mutex);
        return NULL;
    }
    cpayload->uid = uid;
    cpayload->gid = gid;
    /* Make copy of credential */
    cache_cred = PINT_dup_credential(credential);
    if (cache_cred == NULL)
    {
        gossip_lerr("out of memory\n");
        gen_posix_mutex_unlock(&credcache_mutex);
        return NULL;
    }
    cpayload->credential = cache_cred;

    /* have cache entry expire before credential to avoid 
       using credential that's about to expire */
    tval.tv_sec = credential->timeout - CRED_TIMEOUT_BUFFER;
    tval.tv_usec = 0;

    ret = PINT_tcache_insert_entry_ex(
        credential_cache,
        &ckey,
        cpayload,
        &tval,
        &status);

    if (ret == 0)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "cached credential for (%u, %u)\n",
                     uid, gid);
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "cache insert returned %d\n", ret);
    }

#ifdef ENABLE_SECURITY_CERT
    } /* if */
#endif

    gen_posix_mutex_unlock(&credcache_mutex);
    return credential;
}

/* remove credential from cache */
void remove_credential(
    PVFS_uid uid,
    PVFS_gid gid)
{
    struct credential_key ckey;
    struct PINT_tcache_entry *entry;
    int status, ret;

    gen_posix_mutex_lock(&credcache_mutex);

    gossip_debug(GOSSIP_SECURITY_DEBUG, "removing credential (%u, %u) from "
                 "cache...\n", uid, gid);

    ckey.uid = uid;
    ckey.gid = gid;

    /* lookup credential */
    ret = PINT_tcache_lookup(credential_cache, &ckey, &entry, &status);

    if (ret == 0)
    {
        ret = PINT_tcache_delete(credential_cache, entry);
        gossip_debug(GOSSIP_SECURITY_DEBUG, "... cache delete returned %d\n", 
                     ret);
    }
    else
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, "... cache lookup returned %d\n", 
                     ret);
    }

    gen_posix_mutex_unlock(&credcache_mutex);
}
