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

#define PINT_PCACHE_MAX_ENTRIES 64

#define PINT_PCACHE_HANDLE_INVALID 0
#define BAD_LINK -1
#define PINT_PCACHE_TIMEOUT 5
#define ECACHEFULL 1

enum
{
    STATUS_UNUSED = 0,
    STATUS_USED = 1
};

enum
{
    PINODE_EXPIRED = 0,
    PINODE_VALID = 1
};

enum
{
    PCACHE_LOOKUP_FAILURE = 0,
    PCACHE_LOOKUP_SUCCESS = 1
};

/* Pcache Related Declarations */

/* Pinode Cache Element */
struct cache_t
{
    pinode *pnode;
    int prev;
    int next;
    int ref_count;
    int status;
};

/* Pinode Cache Management structure */
struct pinodecache {
    struct cache_t element[PINT_PCACHE_MAX_ENTRIES];
    int count;
    int top;
    int bottom;
    gen_mutex_t *mt_lock;/* Mutex */
};
typedef struct pinodecache pcache;

/* Public Interface */
int PINT_pcache_initialize(void);
int PINT_pcache_finalize(void);
int PINT_pcache_lookup(pinode_reference refn,pinode **pinode_ptr);
int PINT_pcache_lookup_rls(pinode *pinode_ptr);
int PINT_pcache_insert(pinode *pnode);
int PINT_pcache_insert_rls(pinode *pnode);
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
