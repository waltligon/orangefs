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

#include "gen-locks.h"
#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pint-sysint.h"

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

/* Public Interface */
int PINT_pcache_initialize(void);
int PINT_pcache_finalize(void);
int PINT_pcache_lookup(pinode_reference refn,pinode *pinode_ptr);
int PINT_pcache_insert(pinode *pnode);
int PINT_pcache_remove(pinode_reference refn,pinode **item);
int PINT_pcache_pinode_alloc(pinode **pnode);
void PINT_pcache_pinode_dealloc(pinode *pnode);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
