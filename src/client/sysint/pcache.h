/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef __PCACHE_H
#define __PCACHE_H

#include <limits.h>
#include <string.h>
#include <gen-locks.h>
#include <pvfs2-types.h>
#include <pvfs2-attr.h>
#include <pint-sysint.h>

/* Public Interface */
int pcache_initialize(pcache *cache);
int pcache_finalize(pcache cache);
int pcache_lookup(pcache *cache,pinode_reference refn,pinode *pinode_ptr);
int pcache_insert(pcache *cache, pinode *pnode);
int pcache_remove(pcache *cache, pinode_reference refn,pinode **item);
int pcache_pinode_alloc(pinode **pnode);
void pcache_pinode_dealloc(pinode *pnode);

#endif
