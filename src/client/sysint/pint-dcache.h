/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef _PINT_DCACHE_H
#define _PINT_DCACHE_H

#include <pint-sysint.h>

/* number of entries allowed in the cache */
#define PINT_DCACHE_MAX_ENTRIES 512

/* number of seconds that cache entries will remain valid */
#define PINT_DCACHE_TIMEOUT 5

/* TODO: replace later with real value from trove */
/* value passed out to indicate lookups that didn't find a match */
#define PINT_DCACHE_HANDLE_INVALID 0

/* Function Prototypes */
int PINT_dcache_lookup(
	char *name, 
	PVFS_pinode_reference parent,
	PVFS_pinode_reference *entry);

int PINT_dcache_insert(
	char *name, 
	PVFS_pinode_reference entry,
	PVFS_pinode_reference parent);

int PINT_dcache_flush(void);

int PINT_dcache_remove(
	char *name, 
	PVFS_pinode_reference parent,
	int *item_found);

int PINT_dcache_initialize(void);

int PINT_dcache_finalize(void);

int PINT_dcache_get_timeout(void);

void PINT_dcache_set_timeout(int max_timeout_ms);

#endif 
