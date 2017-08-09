/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Server-side credential cache declarations
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CREDCACHE

#ifndef _CREDCACHE_H
#define _CREDCACHE_H

#include <stdint.h>
#include <time.h>

#include "pvfs2-types.h"
#include "seccache.h"

#ifndef CREDCACHE_TIMEOUT
#define CREDCACHE_TIMEOUT    300
#endif

/* externally visible credential cache API */
int PINT_credcache_init(void);
int PINT_credcache_finalize(void);
seccache_entry_t *PINT_credcache_lookup(PVFS_credential *cred);
int PINT_credcache_insert(const PVFS_credential *cred);
int PINT_credcache_remove(seccache_entry_t *entry);

#endif /* _CREDCACHE_H */
#endif /* ENABLE_CREDCACHE */

