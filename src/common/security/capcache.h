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

/* Externally Visible Capability Cache API */
int PINT_capcache_init(void);

int PINT_capcache_finalize(void);

seccache_entry_t *PINT_capcache_lookup(PVFS_capability *cap);

int PINT_capcache_insert(PVFS_capability *cap);

int PINT_capcache_quick_sign(PVFS_capability *cap);

/* End of Externally Visible Capability Cache API */
#endif /* _CAPCACHE_H */

#endif /* ENABLE_CAPCACHE */
