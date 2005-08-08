/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#ifndef __ACACHE_H
#define __ACACHE_H

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "gen-locks.h"
#include "quicklist.h"
#include "quickhash.h"

/**
 * The acache API manages client side caching of attributes for handles 
 * (datafile, metafile, symlink, directory, etc.).  Using the acache
 * requires first calling PINT_acache_initialize.  This will initialize
 * the cache hashtable and setup mutexes.  The associated PINT_acache_finalize
 * function must be called for cleanup once all acache operations are done.
 * 
 * The default lifespan of a
 * valid acache entry is PINT_ACACHE_TIMEOUT_MS seconds, but you can
 * set the timeout (at millisecond granularity) at runtime by calling
 * PINT_acache_set_timeout. 
 * You can also retrieve the acache timeout at any time by calling 
 * PINT_acache_get_timeout.
 *
 * 

  How to use the acache in 5 steps of less:
  -----------------------------------------

  - first acquire an existing PINT_pinode object by calling
    PINT_acache_lookup(), or a newly allocated one with
    PINT_acache_pinode_alloc()
  - if existing, check whether that pinode object is valid
    by calling PINT_acache_pinode_status()

  NOTE: this has been changed -- if lookup success, the status will be
  returned in the specified status ptr (if any).  this is solely to
  avoid calling the status method (more efficient).  however, the
  status function can still be called at any time

  - if it's valid, do your business with it, and the call
    PINT_acache_release() on it
  - if it's NOT valid, you can update the contents of the pinode
    and call PINT_acache_set_valid() to properly add it to the acache,
    or just call PINT_acache_release to get rid of it
  - don't call PINT_acache_release on a freshly allocated pinode
    that you've just done a set_valid on, otherwise, it won't remain
    in the acache.

  tips and tricks of the acache hacking gurus:
  --------------------------------------------
  update_timestamps internally bumps up the pinode reference count to
  make sure it stays in the pinode cache.  on expiration, the ref
  count is dropped.  these internal ref counts are separate from the
  user influenced ref counts (i.e. lookup, invalidate, release)


  if you define PINT_ACACHE_AUTO_CLEANUP, the following applies: since
  the number of pinodes in existance at any time is unbounded, if the
  internal allocator sees that a multiple of
  PINT_ACACHE_NUM_FLUSH_ENTRIES pinodes exist, we secretly try to
  reclaim up to PINT_ACACHE_NUM_FLUSH_ENTRIES by scanning the htable
  of used pinodes and freeing any expired, invalid, or pinodes that
  are no longer being referenced.  If none meet these requirements,
  nothing is reclaimed.  The important thing is that we've tried.
  this 'feature' is mostly untested and may not be beneficial.

  PINT_ACACHE_NUM_ENTRIES is a soft limit.  when there are more
  allocated entries than this number, the reclaim becomes much more
  aggressive, but we still can't guarantee to free any entries if
  they're all valid (as callers have references that we can't pull out
  from under them).  in practice, the reclaim appears to work very
  well, but if it becomes a problem and we need an absolute hard
  limit, we could fail the allocations of new pinodes (and modify the
  caller code to better handle allocation failures)
*/

#define PINT_ACACHE_TIMEOUT_MS                                  5000
#define PINT_ACACHE_NUM_ENTRIES                                10240
#define PINT_ACACHE_NUM_FLUSH_ENTRIES  (PINT_ACACHE_NUM_ENTRIES / 4)
#define PINT_ACACHE_HTABLE_SIZE                                 4093

enum
{
    PINODE_STATUS_VALID               = 3,
    PINODE_STATUS_INVALID             = 4,
    PINODE_STATUS_EXPIRED             = 5,
    PINODE_INTERNAL_FLAG_HASHED       = 6,
    PINODE_INTERNAL_FLAG_UNHASHED     = 7,
    PINODE_INTERNAL_FLAG_EMPTY_LOOKUP = 8
};

const char *PINT_acache_get_status(int status);

typedef struct
{
    int status;
    int flag;
    int ref_cnt;
    PVFS_size size;
    gen_mutex_t *mutex;
    struct timeval time_stamp;
    struct PVFS_object_attr attr;
    PVFS_object_ref refn;

    struct qlist_head link;

} PINT_pinode;

int PINT_acache_initialize(void);
void PINT_acache_finalize(void);
int PINT_acache_reinitialize(void);
int PINT_acache_get_timeout(void);
void PINT_acache_set_timeout(int max_timeout_ms);
int PINT_acache_get_size(void);

PINT_pinode *PINT_acache_lookup(
    PVFS_object_ref refn, int *status, int *unexpired_masks);
int PINT_acache_pinode_status(PINT_pinode *pinode, int *unexpired_masks);
void PINT_acache_set_valid(PINT_pinode *pinode);
void PINT_acache_invalidate(PVFS_object_ref refn);
PINT_pinode *PINT_acache_pinode_alloc(void);
void PINT_acache_release(PINT_pinode *pinode);
int PINT_acache_insert(PVFS_object_ref refn, PVFS_object_attr *attr);
void PINT_acache_free_pinode(PINT_pinode *pinode);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif /* __ACACHE_H */
