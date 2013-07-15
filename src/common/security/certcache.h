/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Certificate cache declarations
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CERTCACHE

#ifndef _CERTCACHE_H
#define _CERTCACHE_H

#include <stdint.h>
#include <time.h>

#include <openssl/asn1.h>

#include "seccache.h"
#include "pvfs2-types.h"
#include "llist.h"

/* certcache Macros - Adjust accordingly */
/* Number of certificate cache entries certificate cache can hold */
#define CERTCACHE_ENTRY_LIMIT    256
/* The total certificate cache size 64 MB */
#define CERTCACHE_SIZE_LIMIT     (1024 * 1024 * 64)
/* Number of indexes in our chained hash-table */
#define CERTCACHE_HASH_LIMIT     128
/* Maximum size (in chars) of cert subject */
#define CERTCACHE_SUBJECT_SIZE   512
/* Frequency (in lookups) to debugging stats */
#define CERTCACHE_STATS_FREQ     1000

/* certificate cache entry data */
typedef struct certcache_data_s {
    ASN1_UTCTIME *expiration;
    char subject[CERTCACHE_SUBJECT_SIZE];
    PVFS_uid uid;
    uint32_t num_groups;
    PVFS_gid *group_array;
} certcache_data_t;

/* Externally Visible Certificate Cache API */
int PINT_certcache_init(void);
int PINT_certcache_finalize(void);
seccache_entry_t * PINT_certcache_lookup(PVFS_certificate *cert);
int PINT_certcache_insert(const PVFS_certificate * cert, 
                          PVFS_uid uid,
                          uint32_t num_groups,
                          PVFS_gid * group_array);
int PINT_certcache_remove(seccache_entry_t * entry);
/* End of Externally Visible Certificate Cache API */

#endif /* _CERTCACHE_H */
#endif /* ENABLE_CERTCACHE */
