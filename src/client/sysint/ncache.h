/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef _PINT_NCACHE_H
#define _PINT_NCACHE_H

#include "pint-sysint-utils.h"

/* number of entries allowed in the cache */
#define PINT_NCACHE_MAX_ENTRIES 512

/* number of seconds that cache entries will remain valid */
#define PINT_NCACHE_TIMEOUT 5

/* TODO: replace later with real value from trove */
/* value passed out to indicate lookups that didn't find a match */
#define PINT_NCACHE_HANDLE_INVALID 0

/* Function Prototypes */
int PINT_ncache_lookup(
	char *name, 
	PVFS_object_ref parent,
	PVFS_object_ref *entry);

int PINT_ncache_insert(
	char *name, 
	PVFS_object_ref entry,
	PVFS_object_ref parent);

int PINT_ncache_flush(void);

int PINT_ncache_remove(
	char *name, 
	PVFS_object_ref parent,
	int *item_found);

int PINT_ncache_initialize(void);

int PINT_ncache_finalize(void);

int PINT_ncache_get_timeout(void);

void PINT_ncache_set_timeout(int max_timeout_ms);

#endif 
