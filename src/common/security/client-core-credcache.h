#ifndef CLIENT_CORE_CREDCACHE_H
#define CLIENT_CORE_CREDCACHE_H

#include <pvfs2-types.h>
#include <tcache.h>
#include <gen-locks.h>

#define CRED_TIMEOUT_BUFFER 5

typedef struct
{
    int ccache_timeout;
    int ccache_timeout_set;
    unsigned int ccache_hard_limit;
    int ccache_hard_limit_set;
    unsigned int ccache_soft_limit;
    int ccache_soft_limit_set;
    unsigned int ccache_reclaim_percentage;
    int ccache_reclaim_percentage_set;
    char *keypath;
} credcache_options_t;

struct credential_key
{
    PVFS_uid uid;
    PVFS_gid gid;
};

struct credential_payload
{
    PVFS_uid uid;
    PVFS_gid gid;
    PVFS_credential *credential;
};

PVFS_credential *generate_credential(
    PVFS_uid uid,
    PVFS_gid gid);

PVFS_credential *lookup_credential(
    PVFS_uid uid,
    PVFS_gid gid);

void remove_credential(
    PVFS_uid uid,
    PVFS_gid gid);

extern credcache_options_t credcache_opts;
extern struct PINT_tcache *credential_cache;
extern gen_mutex_t credcache_mutex; /* TODO Do we need this? */

#endif