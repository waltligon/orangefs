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
#define PINT_DCACHE_MAX_ENTRIES 64

/* number of seconds that cache entries will remain valid */
#define PINT_DCACHE_TIMEOUT 5

/* TODO: replace later with real value from trove */
/* value passed out to indicate lookups that didn't find a match */
#define PINT_DCACHE_HANDLE_INVALID 0

/* TODO: move these definitions into the c file */

/* Dcache Entry */
struct dcache_entry_s {
	pinode_reference parent;   /* the pinode of the parent directory */
	char name[PVFS_NAME_MAX];  /* PVFS object name */
	pinode_reference entry;    /* the pinode of entry in parent */
	struct timeval tstamp_valid;  /* timestamp indicating validity period */
};
typedef struct dcache_entry_s dcache_entry;

/* Dcache element */
struct dcache_t {
	dcache_entry dentry;
	int16_t prev;
	int16_t next;
};

/* Cache Management structure */
struct dcache {
	struct dcache_t element[PINT_DCACHE_MAX_ENTRIES];
	int count;
	int16_t top;
	int16_t free;
	int16_t bottom;
	gen_mutex_t *mt_lock;
};
typedef struct dcache dcache;


/* Function Prototypes */
int PINT_dcache_lookup(
	char *name, 
	pinode_reference parent,
	pinode_reference *entry);

int PINT_dcache_insert(
	char *name, 
	pinode_reference entry,
	pinode_reference parent);

int PINT_dcache_flush(void);

int PINT_dcache_remove(
	char *name, 
	pinode_reference parent,
	int *item_found);

int PINT_dcache_initialize(void);

int PINT_dcache_finalize(void);

#endif 
