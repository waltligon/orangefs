/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef __PCACHE_H
#define __PCACHE_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gen-locks.h"
#include "quicklist.h"
#include "quickhash.h"

/*
  Using the pcache 2.0 for dummies
  ================================

  PINT_pcache_initialize must be the first called method, and
  PINT_pcache_finalize must be the last.  The default lifespan
  of a valid pcache entry is PINT_PCACHE_TIMEOUT seconds, but
  you can set the timeout (at millisecond granularity) at runtime
  by called PINT_pcache_set_timeout. You can also retrieve the
  pcache timeout at any time by calling PINT_pcache_get_timeout.

  How to use the pcache in 5 steps of less:
  -----------------------------------------

  - first acquire an existing PINT_pinode object by calling
    PINT_pcache_lookup(), or a newly allocated one with
    PINT_pcache_pinode_alloc()
  - if existing, check whether that pinode object is valid
    by calling PINT_pcache_pinode_status()
  - if it's valid, do your business with it, and the call
    PINT_pcache_release() on it
  - if it's NOT valid, you can update the contents of the pinode
    and call PINT_pcache_set_valid() to properly add it to the pcache,
    or just call PINT_pcache_release to get rid of it
  - whether it's valid or not, you MUST call PINT_pcache_release()
    on the pinode returned from lookup when you're finished with it

  tips and tricks of the pcache hacking gurus:
  --------------------------------------------
  update_timestamps internally bumps up the pinode reference count to
  make sure it stays in the pinode cache.  on expiration, the ref count
  is dropped.  these internal ref counts are separate from the user
  influenced ref counts (i.e. lookup, invalidate, release)
*/

#define PINT_PCACHE_TIMEOUT                                        5
#define PINT_PCACHE_NUM_ENTRIES                                   16
#define PINT_PCACHE_NUM_FLUSH_ENTRIES  (PINT_PCACHE_NUM_ENTRIES / 4)
/* #define PINT_PCACHE_MAX_ENTRIES      512 */
#define PINT_PCACHE_HTABLE_SIZE                                  127

enum
{
    PINODE_STATUS_VALID               = 3,
    PINODE_STATUS_INVALID             = 4,
    PINODE_STATUS_EXPIRED             = 5,
    PINODE_INTERNAL_FLAG_HASHED       = 6,
    PINODE_INTERNAL_FLAG_UNHASHED     = 7,
    PINODE_INTERNAL_FLAG_EMPTY_LOOKUP = 8
};

typedef struct
{
    int status;
    int flag;
    int ref_cnt;
    PVFS_size size;
    gen_mutex_t *mutex;
    struct timeval time_stamp;
    struct PVFS_object_attr attr;
    PVFS_pinode_reference refn;

    struct qlist_head link;

} PINT_pinode;

int PINT_pcache_initialize(void);
void PINT_pcache_finalize(void);
int PINT_pcache_get_timeout(void);
void PINT_pcache_set_timeout(int max_timeout_ms);
int PINT_pcache_get_size(void);

PINT_pinode *PINT_pcache_lookup(PVFS_pinode_reference refn);
int PINT_pcache_pinode_status(PINT_pinode *pinode);
void PINT_pcache_set_valid(PINT_pinode *pinode);
void PINT_pcache_invalidate(PVFS_pinode_reference refn);
PINT_pinode *PINT_pcache_pinode_alloc(void);
void PINT_pcache_release_refn(PVFS_pinode_reference refn);
void PINT_pcache_release(PINT_pinode *pinode);


/* these should be moved to pvfs2-utils */
int PINT_pcache_object_attr_deep_copy(
    PVFS_object_attr *dest,
    PVFS_object_attr *src);
void PINT_pcache_object_attr_deep_free(
    PVFS_object_attr *attr);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif /* __PCACHE_H */
