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

/* Pcache Related Declarations */
/* Pinode structure */
typedef struct
{
        pinode_reference pinode_ref; /* pinode reference - entity to

          uniquely identify a pinode */
        gen_mutex_t *pinode_mutex;        /* mutex lock */
        struct PVFS_object_attr attr;   /* attributes of PVFS object */
        PVFS_bitfield mask;                       /* attribute mask */
        PVFS_size size;                           /* PVFS object size */
        struct timeval tstamp_handle;/* timestamp for handle consistency */
        struct timeval tstamp_attr;  /* timestamp for attribute consistency */
        struct timeval tstamp_size;  /* timestamp for size consistency */
} pinode, *pinode_p;

/* Pinode Cache Element */
struct cache_t
{
        pinode *pnode;
        int16_t prev;
        int16_t next;
};

/* Pinode Cache Management structure */
struct pinodecache {
        struct cache_t element[MAX_ENTRIES];
        int count;
        int16_t top;    /* Add at top */
        int16_t free;   /* Points to next free array element - kind of like
                                                   a free list */
        int16_t bottom; /* Replace at bottom */
        gen_mutex_t *mt_lock;/* Mutex */
};
typedef struct pinodecache pcache;

#endif
