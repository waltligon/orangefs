/*
 * (C) 2014 Clemson University and Omnibond Systems LLC
 *
 * Capability revocation cache declarations
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_REVOCATION

#ifndef _REVLIST_H
#define _REVLIST_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "seccache.h"

/* revocation definitions */
#define PINT_REVLIST_NOT_FOUND 0
#define PINT_REVLIST_FOUND     1

/* revocation list entry */
typedef struct revocation_data_s {
    char *server;
    PVFS_capability_id cap_id;
    PVFS_time expiration;
} revocation_data_t;

/* revocation list API */

int PINT_revlist_init(void);

int PINT_revlist_finalize(void);

seccache_entry_t *PINT_revlist_lookup(const char *server,
									  PVFS_capability_id cap_id);

int PINT_revlist_insert(const char *server, 
                        PVFS_capability_id cap_id,
                        PVFS_time expiration);

int PINT_revlist_remove(seccache_entry_t *entry);

#endif /* _REVLIST_H */

#endif /* ENABLE_REVOCATION */
