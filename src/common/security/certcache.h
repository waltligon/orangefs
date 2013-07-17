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

#define CERTCACHE_SUBJECT_SIZE    512

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
/* End of Externally Visible Certificate Cache API */

#endif /* _CERTCACHE_H */
#endif /* ENABLE_CERTCACHE */
