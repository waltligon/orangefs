/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Capability cache declarations
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CAPCACHE

#ifndef _CAPCACHE_H
#define _CAPCACHE_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "seccache.h"

/* capcache property defaults */
/* Default timeout of capability-cache entry in seconds */
#ifndef CAPCACHE_TIMEOUT
#define CAPCACHE_TIMEOUT       10
#endif

/* capcache flags */
#define CAPCACHE_ISSUED       0x1
#define CAPCACHE_VALID        0x2
#define CAPCACHE_REVOKED      0x4

#define CAPCACHE_LOOKUP_NOT_FOUND  0
#define CAPCACHE_LOOKUP_FOUND      1
#define CAPCACHE_LOOKUP_REVOKED    2

/* capcache entry structure */
typedef struct capcache_data_s {
    int flags;
    PVFS_capability *cap;
} capcache_data_t;

/* Externally Visible Capability Cache API */
int PINT_capcache_init(void);

int PINT_capcache_finalize(void);

int PINT_capcache_lookup(PVFS_capability *cap,
                         int *cap_flags);

int PINT_capcache_insert(PVFS_capability *cap,
                         int cap_flags);

#if defined(ENABLE_SECURITY_KEY) || defined(ENABLE_SECURITY_CERT)
int PINT_capcache_quick_sign(PVFS_capability *cap);
#endif

#ifdef ENABLE_REVOCATION
int PINT_capcache_lookup_by_handle(PVFS_handle handle,
                                   PVFS_capability **cap);

int PINT_capcache_revoke_cap(const char *server,
                             PVFS_capability_id cap_id);
#endif

/* End of Externally Visible Capability Cache API */
#endif /* _CAPCACHE_H */

#endif /* ENABLE_CAPCACHE */
