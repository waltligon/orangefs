/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef _PINT_DCACHE_H
#define _PINT_DCACHE_H

#include <pcache.h>

/* Timeout for a dcache entry */
struct timeval dentry_to;

/* Function Prototypes */
int dcache_lookup(struct dcache *cache, char *name, pinode_reference parent,\
		pinode_reference *entry);
int dcache_insert(struct dcache *cache, char *name, pinode_reference entry,\
		pinode_reference parent);
int dcache_flush(struct dcache cache);
int dcache_remove(struct dcache *cache, char *name, pinode_reference parent,\
		unsigned char *item_found);
int dcache_initialize(struct dcache *cache);
int dcache_finalize(struct dcache cache);

#endif 
