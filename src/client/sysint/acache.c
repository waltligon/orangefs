/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#include <limits.h>
#include <string.h>

#include <assert.h>

#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pint-distribution.h"
#include "pint-sysint-utils.h"
#include "gen-locks.h"
#include "acache.h"
#include "quickhash.h"
#include "pint-util.h"

/* comment out the following for non-verbose acache debugging */
#define VERBOSE_ACACHE_DEBUG

/*
  comment out the following for an experimental pinode cleanup
  mechanism for trying to bound the number of pinode entries in the
  acache at any given time
*/
#define PINT_ACACHE_AUTO_CLEANUP


#ifdef VERBOSE_ACACHE_DEBUG
#define acache_debug(x) gossip_debug(GOSSIP_ACACHE_DEBUG,x)
#else
#define acache_debug(...)
#endif

const char * PINT_acache_status_strings[6] =
{
    "PINODE_STATUS_VALID",
    "PINODE_STATUS_INVALID",
    "PINODE_STATUS_EXPIRED",
    "PINODE_INTERNAL_FLAG_HASHED",
    "PINODE_INTERNAL_FLAG_UNHASHED",
    "PINODE_INTERNAL_FLAG_EMPTY_LOOKUP"
};

#define ACACHE_TIMEVAL_IS_EXPIRED(_nowstamp, _expirestamp) \
    (((_nowstamp.tv_sec == _expirestamp.tv_sec) && \
      (_nowstamp.tv_usec > _expirestamp.tv_usec)) ? 1 : \
     ((_nowstamp.tv_sec > _expirestamp.tv_sec) ? 1 : 0))

static struct qhash_table *s_acache_htable = NULL;
static gen_mutex_t *s_acache_htable_mutex = NULL;

static gen_mutex_t s_acache_interface_mutex = GEN_MUTEX_INITIALIZER;

static int s_acache_initialized = 0;
static int s_acache_timeout_ms = PINT_ACACHE_TIMEOUT_MS;
static int s_acache_allocated_entries = 0;

static int pinode_hash_refn(
    void *refn_p, int table_size);
static int pinode_hash_refn_compare(
    void *key, struct qlist_head *link);
static PINT_pinode *pinode_alloc(void);
static void pinode_free(
    PINT_pinode *pinode);
static int pinode_status(
    PINT_pinode *pinode,
    int *unexpired_masks);
static void pinode_update_timestamp(
    PINT_pinode **pinode);
static void pinode_invalidate(
    PINT_pinode *pinode);
static inline void acache_internal_release(
    PINT_pinode *pinode);
static inline PINT_pinode *acache_internal_lookup(
    PVFS_object_ref refn);
static inline int acache_internal_status(
    PINT_pinode *pinode, int *unexpired_masks);
#ifdef PINT_ACACHE_AUTO_CLEANUP
static void reclaim_pinode_entries(void);
#endif

const char *PINT_acache_get_status(int status)
{
    return PINT_acache_status_strings[status - 3];
}

/*
  initializes acache; MUST be called before
  using any other acache methods.

  returns 0 on success; -1 otherwise
*/
int PINT_acache_initialize()
{
    int ret = -1;

    acache_debug("PINT_acache_initialize entered\n");

    ret = PINT_cached_config_initialize();
    if(ret < 0)
    {
        return ret;
    }

    gen_mutex_lock(&s_acache_interface_mutex);

    s_acache_htable_mutex = gen_mutex_build();
    if (!s_acache_htable_mutex)
    {
        gen_mutex_unlock(&s_acache_interface_mutex);
        return ret;
    }

    s_acache_htable = qhash_init(
        pinode_hash_refn_compare,
        pinode_hash_refn, PINT_ACACHE_HTABLE_SIZE);
    assert(s_acache_htable);
    if (!s_acache_htable)
    {
        goto error_exit;
    }

    s_acache_initialized = 1;
    gen_mutex_unlock(&s_acache_interface_mutex);
    acache_debug("PINT_acache_initialize exiting\n");
    return 0;

  error_exit:
    gen_mutex_unlock(&s_acache_interface_mutex);
    acache_debug("PINT_acache_initialize error exiting\n");
    PINT_acache_finalize();
    return ret;
}

void PINT_acache_finalize()
{
    int i = 0;
    PINT_pinode *pinode = NULL;
    struct qlist_head *link = NULL, *tmp = NULL;

    acache_debug("PINT_acache_finalize entered\n");

    gen_mutex_lock(&s_acache_interface_mutex);

    if (s_acache_htable_mutex && s_acache_htable)
    {
        gen_mutex_lock(s_acache_htable_mutex);
        for(i = 0; i < s_acache_htable->table_size; i++)
        {
            qhash_for_each_safe(link, tmp, &(s_acache_htable->array[i]))
            {
                pinode = qlist_entry(link, PINT_pinode, link);
                assert(pinode);

                pinode_free(pinode);
            }
        }
        gen_mutex_unlock(s_acache_htable_mutex);
        gen_mutex_destroy(s_acache_htable_mutex);
        qhash_finalize(s_acache_htable);
    }

    /* make sure all pinodes are accounted for */
    assert(s_acache_allocated_entries == 0);
    if (s_acache_allocated_entries == 0)
    {
        acache_debug("All acache entries freed properly\n");
    }
    else
    {
        acache_debug("Lost some acache entries; memory leak\n");
    }

    s_acache_htable = NULL;
    s_acache_htable_mutex = NULL;

    gen_mutex_unlock(&s_acache_interface_mutex);
    acache_debug("PINT_acache_finalize exiting\n");

    PINT_cached_config_finalize();
}

int PINT_acache_reinitialize(void)
{
    PINT_acache_finalize();
    return PINT_acache_initialize();
}

/*
  internal use only -- does a lookup without involving the pinode
  locks or reference counts; always done with the interface lock and
  without the htable mutex held
*/
static inline PINT_pinode *acache_internal_lookup(PVFS_object_ref refn)
{
    PINT_pinode *pinode = NULL;
    struct qhash_head *link = NULL;

    acache_debug("acache_internal_lookup entered\n");
    assert(s_acache_initialized);

    gen_mutex_lock(s_acache_htable_mutex);
    link = qhash_search(s_acache_htable, &refn);
    if (link)
    {
        pinode = qlist_entry(link, PINT_pinode, link);
        assert(pinode);
        gossip_debug(GOSSIP_ACACHE_DEBUG, "*** acache internal lookup "
                     "found pinode [%Lu]\n", Lu(pinode->refn.handle));
    }
    gen_mutex_unlock(s_acache_htable_mutex);

    if (pinode)
    {
        assert(pinode->flag = PINODE_INTERNAL_FLAG_HASHED);
    }
    acache_debug("acache_internal_lookup exiting\n");
    return pinode;
}

/*
  returns the pinode matching the specified reference if present in
  the acache. returns NULL otherwise.

  NOTE: if a pinode is returned, it is returned with the lock held.
  That means no one else can use it before the lock is released (in
  release, or set_valid).  if status is specified, the pinode status
  will be filled to avoid calling the status method after this call
*/
PINT_pinode *PINT_acache_lookup(
    PVFS_object_ref refn, int *status, int *unexpired_masks)
{
    PINT_pinode *pinode = NULL;
    struct qhash_head *link = NULL;

    acache_debug("PINT_acache_lookup entered\n");

    gen_mutex_lock(&s_acache_interface_mutex);
    assert(s_acache_initialized);

    gen_mutex_lock(s_acache_htable_mutex);
    link = qhash_search(s_acache_htable, &refn);
    if (link)
    {
        pinode = qlist_entry(link, PINT_pinode, link);
        assert(pinode);
        gossip_debug(GOSSIP_ACACHE_DEBUG, "*** acache lookup found "
                     "pinode [%Lu]\n", Lu(pinode->refn.handle));
    }
    gen_mutex_unlock(s_acache_htable_mutex);

    if (pinode)
    {
        gen_mutex_trylock(pinode->mutex);
        assert(pinode->flag = PINODE_INTERNAL_FLAG_HASHED);
        pinode->ref_cnt++;
        if (status)
        {
            *status = acache_internal_status(pinode, unexpired_masks);
        }
    }
    gen_mutex_unlock(&s_acache_interface_mutex);
    acache_debug("PINT_acache_lookup exiting\n");
    return pinode;
}

PINT_pinode *PINT_acache_pinode_alloc()
{
#ifdef PINT_ACACHE_AUTO_CLEANUP
    gen_mutex_lock(&s_acache_interface_mutex);
    /*
      PINT_ACACHE_NUM_FLUSH_ENTRIES is a soft limit that triggers an
      attempt to reclaim any expired or invalidated entries that
      currently reside in the acache.
    */
    if (((s_acache_allocated_entries &&
          (s_acache_allocated_entries %
           PINT_ACACHE_NUM_FLUSH_ENTRIES) == 0)) ||
        (s_acache_allocated_entries > PINT_ACACHE_NUM_ENTRIES))
    {
        reclaim_pinode_entries();
    }
    gen_mutex_unlock(&s_acache_interface_mutex);
#endif
    return pinode_alloc();
}

void PINT_acache_free_pinode(PINT_pinode *pinode)
{
    pinode_free(pinode);
}

int PINT_acache_pinode_status(PINT_pinode *pinode, int *unexpired_masks)
{
    assert(s_acache_initialized);
    acache_debug("PINT_acache_status called\n");
    return pinode_status(pinode, unexpired_masks);
}

void PINT_acache_set_valid(PINT_pinode *pinode)
{
    acache_debug("PINT_acache_set_valid entered\n");
    assert(s_acache_initialized);

    gen_mutex_lock(&s_acache_interface_mutex);
    if (pinode)
    {
        /* if we don't have the lock, acquire it */
        gen_mutex_trylock(pinode->mutex);

        /*
          if it's hashed, that probably means the caller got a
          valid pinode entry from a lookup but called us anyway;
          otherwise, add the pinode the htable
        */
        if (pinode->flag == PINODE_INTERNAL_FLAG_UNHASHED)
        {
            gen_mutex_lock(s_acache_htable_mutex);
            qhash_add(s_acache_htable, &pinode->refn, &pinode->link);
            gen_mutex_unlock(s_acache_htable_mutex);

            pinode->flag = PINODE_INTERNAL_FLAG_HASHED;
            gossip_debug(GOSSIP_ACACHE_DEBUG, "+++ added pinode "
                         "to htable [%Lu type: %d] (%d entries)\n",
                         Lu(pinode->refn.handle), pinode->attr.objtype,
                         s_acache_allocated_entries);
        }

        pinode->status = PINODE_STATUS_VALID;
        pinode_update_timestamp(&pinode);
        gen_mutex_unlock(pinode->mutex);
    }
    gen_mutex_unlock(&s_acache_interface_mutex);
    acache_debug("PINT_acache_set_valid exiting\n");
}

/* tries to release the pinode, based on the specified refn */
void PINT_acache_invalidate(PVFS_object_ref refn)
{
    PINT_pinode *pinode = NULL;

    acache_debug("PINT_acache_invalidate entered\n");
    assert(s_acache_initialized);

    gen_mutex_lock(&s_acache_interface_mutex);
    pinode = acache_internal_lookup(refn);
    gen_mutex_unlock(&s_acache_interface_mutex);
    if (pinode)
    {
        /* forcefully expire the entry */
        pinode->status = PINODE_STATUS_EXPIRED;

        gossip_debug(GOSSIP_ACACHE_DEBUG, "--- Invalidating pinode "
                     "entry [%Lu]\n", Lu(pinode->refn.handle));

        PINT_acache_release(pinode);
    }
    acache_debug("PINT_acache_invalidate exiting\n");
}

int PINT_acache_insert(PVFS_object_ref refn,
                       PVFS_object_attr *attr)
{
    PINT_pinode *pinode;
    
    pinode = PINT_acache_pinode_alloc();
    if(!pinode)
    {
        return -PVFS_ENOMEM;
    }

    pinode->refn.fs_id = refn.fs_id;
    pinode->refn.handle = refn.handle;
    
    PINT_copy_object_attr(&pinode->attr, attr);

    PINT_acache_set_valid(pinode);

    return 0;
}

/*
  internal use only -- does a release without the interface lock and
  without the htable mutex held
*/
static inline void acache_internal_release(PINT_pinode *pinode)
{
    acache_debug("acache_internal_release entered\n");
    assert(s_acache_initialized);
    if (pinode && pinode->mutex)
    {
        /*
          if we don't have the lock, acquire it; but one way or
          another, it better be locked below
        */
        gen_mutex_trylock(pinode->mutex);

        if (--pinode->ref_cnt == 0)
        {
            gen_mutex_unlock(pinode->mutex);
            pinode_invalidate(pinode);
        }
        else
        {
            gen_mutex_unlock(pinode->mutex);
        }
    }
    acache_debug("acache_internal_release exited\n");
}

void PINT_acache_release(PINT_pinode *pinode)
{
    acache_debug("PINT_acache_release entered\n");
    assert(s_acache_initialized);

    gen_mutex_lock(&s_acache_interface_mutex);
    acache_internal_release(pinode);
    gen_mutex_unlock(&s_acache_interface_mutex);

    acache_debug("PINT_acache_release exited\n");
}

int PINT_acache_get_timeout()
{
    assert(s_acache_initialized);
    return s_acache_timeout_ms;
}
void PINT_acache_set_timeout(int max_timeout_ms)
{
    assert(s_acache_initialized);
    s_acache_timeout_ms = max_timeout_ms;
}

int PINT_acache_get_size()
{
    return s_acache_allocated_entries;
}

static int pinode_hash_refn(void *refn_p, int table_size)
{
    unsigned long tmp = 0;
    PVFS_object_ref *refn = (PVFS_object_ref *)refn_p;

    tmp += (unsigned long)(refn->handle + refn->fs_id);
    tmp = tmp%table_size;

    return (int)tmp;
}

static int pinode_hash_refn_compare(void *key, struct qlist_head *link)
{
    PINT_pinode *pinode = NULL;
    PVFS_object_ref *refn = (PVFS_object_ref *)key;

    pinode = qlist_entry(link, PINT_pinode, link);
    assert(pinode);

    if ((pinode->refn.handle == refn->handle) &&
        (pinode->refn.fs_id == refn->fs_id))
    {
        return 1;
    }
    return 0;
}

/* never call this directly; internal use only */
static PINT_pinode *pinode_alloc()
{
    PINT_pinode *pinode = NULL;

    pinode = (PINT_pinode *)malloc(sizeof(PINT_pinode));
    if (pinode)
    {
        memset(pinode,0,sizeof(PINT_pinode));
        pinode->mutex = gen_mutex_build();
        if (!pinode->mutex)
        {
            free(pinode);
            pinode = NULL;
        }
        else
        {
            pinode->ref_cnt = 0;
            pinode->status = PINODE_STATUS_INVALID;
            pinode->flag = PINODE_INTERNAL_FLAG_UNHASHED;

            s_acache_allocated_entries++;
        }
    }
    return pinode;
}

/* never call this directly; internal use only */
static void pinode_free(PINT_pinode *pinode)
{
    if (pinode)
    {
        if (pinode->mutex)
        {
            gen_mutex_destroy(pinode->mutex);
            pinode->mutex = NULL;
        }

        PINT_free_object_attr(&pinode->attr);
        free(pinode);
        s_acache_allocated_entries--;
        pinode = NULL;
    }
}

/*
  atomically invalidate the pinode status flag and free the
  pinode.  removes pinode from htable if hashed.
*/
static void pinode_invalidate(PINT_pinode *pinode)
{
    struct qlist_head *link = NULL;

    acache_debug("pinode_invalidate entered\n");
    gen_mutex_lock(pinode->mutex);
    pinode->status = PINODE_STATUS_INVALID;

    if (pinode->flag == PINODE_INTERNAL_FLAG_HASHED)
    {
        gen_mutex_lock(s_acache_htable_mutex);
        link = qhash_search_and_remove(s_acache_htable, &pinode->refn);
        if (link)
        {
            pinode = qlist_entry(link, PINT_pinode, link);
            assert(pinode);
            pinode->flag = PINODE_INTERNAL_FLAG_UNHASHED;
        }
        gen_mutex_unlock(s_acache_htable_mutex);
        acache_debug("*** pinode_invalidate: removed from htable\n");
    }
    gen_mutex_unlock(pinode->mutex);
    pinode_free(pinode);
    acache_debug("*** pinode_invalidate: freed pinode\n");
    acache_debug("pinode_invalidate exited\n");
}

static int pinode_status(PINT_pinode *pinode, int *unexpired_masks)
{
    int ret = PINODE_STATUS_INVALID;

    acache_debug("pinode_status entered\n");

    gen_mutex_trylock(pinode->mutex);
    ret = acache_internal_status(pinode, unexpired_masks);
    gen_mutex_unlock(pinode->mutex);

    acache_debug("pinode_status exited\n");
    return ret;
}

/*
 * unexpired_masks variable allows us to extend the acache
 * interface to allow for different timeouts for different masks
 * in the attribute, besides the attr cache timeout and the
 * handle recycle timeout.
 */
static inline int acache_internal_status(
    PINT_pinode *pinode, int *unexpired_masks)
{
    struct timeval now, handle_timeout, handle_expire, attr_expire;
    int ret = PINODE_STATUS_INVALID;

    acache_debug("acache_internal_status entered\n");

    ret = pinode->status;
    if (ret == PINODE_STATUS_VALID)
    {
        ret = PINODE_STATUS_INVALID;
        if (pinode->ref_cnt > 0)
        {
            assert(gettimeofday(&now, NULL) == 0);

            if(PINT_cached_config_get_handle_timeout(
                    pinode->refn.fs_id, &handle_timeout) != 0)
            {
                return ret; 
            }
            
            handle_expire.tv_sec = 
                pinode->time_stamp.tv_sec + handle_timeout.tv_sec;
            handle_expire.tv_usec =
                pinode->time_stamp.tv_usec + handle_timeout.tv_usec;
            
            if(ACACHE_TIMEVAL_IS_EXPIRED(now, handle_expire))
            {
                /* all masks are expired if the handle recycle timeout
                 * is expired
                 */
                if(unexpired_masks)
                {
                    *unexpired_masks = 0;
                }
                ret = PINODE_STATUS_EXPIRED;
            }
            else
            {
                if(unexpired_masks)
                {
                    *unexpired_masks = 
                        ((PVFS_ATTR_META_ALL|PVFS_ATTR_COMMON_TYPE) 
                         & pinode->attr.mask);
                }
                ret = PINODE_STATUS_VALID;

                attr_expire.tv_sec =
                    pinode->time_stamp.tv_sec 
                    + (int)(s_acache_timeout_ms / 1000);
                attr_expire.tv_usec =
                    pinode->time_stamp.tv_usec + 
                    (int)((s_acache_timeout_ms % 1000) * 1000);

                if(!ACACHE_TIMEVAL_IS_EXPIRED(now, attr_expire))
                {
                    *unexpired_masks |= pinode->attr.mask;
                }
            }
        }
    }

    gossip_debug(GOSSIP_ACACHE_DEBUG, "pinode [%Lu] entry status: %s\n",
                 Lu(pinode->refn.handle), PINT_acache_get_status(ret));

    if (ret == PINODE_STATUS_EXPIRED)
    {
        pinode->ref_cnt--;
    }
    acache_debug("acache_internal_status exited\n");
    return ret;
}

/* NOTE: called with the pinode mutex lock held */
static void pinode_update_timestamp(PINT_pinode **pinode)
{
    assert(gettimeofday(&((*pinode)->time_stamp), NULL) == 0);
    (*pinode)->ref_cnt++;
}

#ifdef PINT_ACACHE_AUTO_CLEANUP
/*
  attempts to reclaim up to PINT_ACACHE_NUM_FLUSH_ENTRIES
  stale pinode entries; may not reclaim any.
*/
static void reclaim_pinode_entries()
{
    PINT_pinode *pinode = NULL;
    struct qlist_head *link = NULL, *tmp = NULL;
    int i = 0, num_reclaimed = 0;

    acache_debug("reclaim_pinode_entries entered\n");
    if (s_acache_htable_mutex && s_acache_htable)
    {
        gen_mutex_lock(s_acache_htable_mutex);
        for(i = 0; i < s_acache_htable->table_size; i++)
        {
            qhash_for_each_safe(link, tmp, &(s_acache_htable->array[i]))
            {
                pinode = qlist_entry(link, PINT_pinode, link);
                assert(pinode);

                if (pinode_status(pinode, NULL) != PINODE_STATUS_VALID)
                {
                    gen_mutex_unlock(s_acache_htable_mutex);
                    pinode_invalidate(pinode);
                    gen_mutex_lock(s_acache_htable_mutex);
                    if (num_reclaimed++ == PINT_ACACHE_NUM_FLUSH_ENTRIES)
                    {
                        goto reclaim_exit;
                    }
                }
            }
        }
      reclaim_exit:
        gen_mutex_unlock(s_acache_htable_mutex);
    }
    gossip_debug(GOSSIP_ACACHE_DEBUG, "reclaim_pinode_entries "
                 "reclaimed %d entries\n", num_reclaimed);
    gossip_debug(GOSSIP_ACACHE_DEBUG, "Total allocated is %d\n",
                 s_acache_allocated_entries);
    acache_debug("reclaim_pinode_entries exited\n");
}
#endif /* PINT_ACACHE_AUTO_CLEANUP */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

