/* 
 * (C) 2015 Clemson University and Omnibond 
 * 
 *  See COPYING in top-level directory.
 */

/* \file
 * \ingroup security
 * 
 *  Orangefs client-side credential cache management routines.
 */

#ifndef __CLIENT_CREDCACHE_H
#define __CLIENT_CREDCACHE_H

#include "pvfs2-types.h"
#include <stddef.h>
#include "gossip.h"
#include "pvfs2-debug.h"
#include "tcache.h"
#include "security-util.h"
#include "pvfs2-util.c"

PVFS_credential *lookup_credential(
    PVFS_uid uid,
    PVFS_gid gid);

void remove_credential(
    PVFS_uid uid,
    PVFS_gid gid);

PVFS_credential *generate_credential(
    PVFS_uid uid,
    PVFS_gid gid);

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




#endif /* __CLIENT_CREDCACHE_H */

/*
 * Local variables:
 *   c-indent-level: 4
 *   c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
