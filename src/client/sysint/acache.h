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

/*
  Using the acache 2.0 for dummies
  ================================

  PINT_acache_initialize must be the first called method, and
  PINT_acache_finalize must be the last.  The default lifespan of a
  valid acache entry is PINT_ACACHE_TIMEOUT_MS seconds, but you can
  set the timeout (at millisecond granularity) at runtime by called
  PINT_acache_set_timeout. You can also retrieve the acache timeout at
  any time by calling PINT_acache_get_timeout.

  How to use the acache in 5 steps of less:
  -----------------------------------------

  - first acquire an existing PINT_pinode object by calling
    PINT_acache_lookup(), or a newly allocated one with
    PINT_acache_pinode_alloc()
  - if existing, check whether that pinode object is valid
    by calling PINT_acache_pinode_status()
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
*/

#define PINT_ACACHE_TIMEOUT_MS                                  5000
#define PINT_ACACHE_NUM_ENTRIES                                 1024
#define PINT_ACACHE_NUM_FLUSH_ENTRIES  (PINT_ACACHE_NUM_ENTRIES / 4)
#define PINT_ACACHE_HTABLE_SIZE                                  511

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
    PVFS_object_ref refn;

    struct qlist_head link;

} PINT_pinode;

int PINT_acache_initialize(void);
void PINT_acache_finalize(void);
int PINT_acache_get_timeout(void);
void PINT_acache_set_timeout(int max_timeout_ms);
int PINT_acache_get_size(void);

PINT_pinode *PINT_acache_lookup(PVFS_object_ref refn);
int PINT_acache_pinode_status(PINT_pinode *pinode);
void PINT_acache_set_valid(PINT_pinode *pinode);
void PINT_acache_invalidate(PVFS_object_ref refn);
PINT_pinode *PINT_acache_pinode_alloc(void);
void PINT_acache_release_refn(PVFS_object_ref refn);
void PINT_acache_release(PINT_pinode *pinode);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif /* __ACACHE_H */
