/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef _PINT_DCACHE_H
#define _PINT_DCACHE_H

#include <pint-sysint.h>

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
	struct dcache_t element[MAX_ENTRIES];
	int count;
	int16_t top;
	int16_t free;
	int16_t bottom;
	gen_mutex_t *mt_lock;
};
typedef struct dcache dcache;


/* Function Prototypes */
int dcache_lookup(struct dcache *cache, char *name, pinode_reference parent,
		pinode_reference *entry);
int dcache_insert(struct dcache *cache, char *name, pinode_reference entry,
		pinode_reference parent);
int dcache_flush(struct dcache cache);
int dcache_remove(struct dcache *cache, char *name, pinode_reference parent,
		unsigned char *item_found);
int dcache_initialize(struct dcache *cache);
int dcache_finalize(struct dcache cache);

#endif 
