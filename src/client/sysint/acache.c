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

/* uncomment the following for verbose acache debugging */
/* #define VERBOSE_ACACHE_DEBUG */

/*
  uncomment the following for an experimental pinode
  cleanup mechanism for trying to bound the number of
  pinode entries in the acache at any given time
*/
#define PINT_ACACHE_AUTO_CLEANUP


#ifdef VERBOSE_ACACHE_DEBUG
#define acache_debug(x) gossip_debug(GOSSIP_ACACHE_DEBUG,x)
#else
#define acache_debug(...)
#endif

/*
  what we could do is disable the use of the acache
  if locks are not available on the system, but for now...
*/
#ifndef __GEN_POSIX_LOCKING__
#error "Cannot use acache without functioning mutex locking"
#endif

static struct qhash_table *s_acache_htable = NULL;
static gen_mutex_t *s_acache_htable_mutex = NULL;

static int s_acache_initialized = 0;
static int s_acache_timeout_ms = (PINT_ACACHE_TIMEOUT * 1000);
static int s_acache_allocated_entries = 0;

/* static internal helper methods */
static int pinode_hash_refn(void *refn_p, int table_size);
static int pinode_hash_refn_compare(void *key, struct qlist_head *link);
static PINT_pinode *pinode_alloc(void);
static void pinode_free(PINT_pinode *pinode);
static int pinode_status(PINT_pinode *pinode);
static void pinode_update_timestamp(PINT_pinode **pinode);
static void pinode_invalidate(PINT_pinode *pinode);

#ifdef PINT_ACACHE_AUTO_CLEANUP
static void reclaim_pinode_entries(void);
#endif


/*
  initializes acache; MUST be called before
  using any other acache methods.

  returns 0 on success; -1 otherwise
*/
int PINT_acache_initialize()
{
    int ret = -1;

    acache_debug("PINT_acache_initialize entered\n");

    s_acache_htable_mutex = gen_mutex_build();
    if (!s_acache_htable_mutex)
    {
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

    acache_debug("PINT_acache_initialize exiting\n");
    s_acache_initialized = 1;
    return 0;

  error_exit:
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
    acache_debug("PINT_acache_finalize exiting\n");
}

/*
  returns the pinode matching the specified reference if
  present in the acache. returns NULL otherwise.

  NOTE: if a pinode is returned, it is returned with the
  lock held.  That means no one else can use it before
  the lock is released (in release, or set_valid);
*/
PINT_pinode *PINT_acache_lookup(PVFS_object_ref refn)
{
    PINT_pinode *pinode = NULL;
    struct qhash_head *link = NULL;

    acache_debug("PINT_acache_lookup entered\n");
    assert(s_acache_initialized);

    gen_mutex_lock(s_acache_htable_mutex);
    link = qhash_search(s_acache_htable, &refn);
    if (link)
    {
        pinode = qlist_entry(link, PINT_pinode, link);
        assert(pinode);
    }
    gen_mutex_unlock(s_acache_htable_mutex);

    if (pinode)
    {
        gen_mutex_lock(pinode->mutex);
        assert(pinode->flag = PINODE_INTERNAL_FLAG_HASHED);
        pinode->ref_cnt++;
    }
    acache_debug("PINT_acache_lookup exiting\n");
    return pinode;
}

PINT_pinode *PINT_acache_pinode_alloc()
{
#ifdef PINT_ACACHE_AUTO_CLEANUP
    /*
      PINT_ACACHE_NUM_FLUSH_ENTRIES is a soft limit that triggers
      an attempt to reclaim any expired or invalidated entries
      that currently reside in the acache.
    */
    if (s_acache_allocated_entries &&
        (s_acache_allocated_entries % PINT_ACACHE_NUM_FLUSH_ENTRIES) == 0)
    {
        reclaim_pinode_entries();
    }
#endif
    return pinode_alloc();
}

void PINT_acache_free_pinode(PINT_pinode *pinode)
{
    pinode_free(pinode);
}

int PINT_acache_pinode_status(PINT_pinode *pinode)
{
    assert(s_acache_initialized);
    acache_debug("PINT_acache_status called\n");
    return pinode_status(pinode);
}

void PINT_acache_set_valid(PINT_pinode *pinode)
{
    acache_debug("PINT_acache_set_valid entered\n");
    assert(s_acache_initialized);
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
            acache_debug("*** added pinode to htable\n");
/*             fprintf(stderr,"There are %d allocated entries\n", */
/*                     s_acache_allocated_entries); */
        }

        pinode->status = PINODE_STATUS_VALID;
        pinode_update_timestamp(&pinode);
        gen_mutex_unlock(pinode->mutex);
    }
    acache_debug("PINT_acache_set_valid exiting\n");
}

/* tries to release the pinode, based on the specified refn */
void PINT_acache_invalidate(PVFS_object_ref refn)
{
    PINT_pinode *pinode = NULL;

    acache_debug("PINT_acache_invalidate entered\n");
    assert(s_acache_initialized);

    pinode = PINT_acache_lookup(refn);
    if (pinode)
    {
        /* drop the ref count we picked up in lookup */
        pinode->ref_cnt--;

        /* forcefully expire the entry */
        pinode->status = PINODE_STATUS_EXPIRED;

        /* we should have the lock at this point */
        gen_mutex_unlock(pinode->mutex);

        PINT_acache_release(pinode);
    }
    acache_debug("PINT_acache_invalidate exiting\n");
}

void PINT_acache_release_refn(PVFS_object_ref refn)
{
    PINT_pinode *pinode = PINT_acache_lookup(refn);
    if (pinode)
    {
        /* drop the ref count we picked up in lookup */
        pinode->ref_cnt--;
        PINT_acache_release(pinode);
    }
}

void PINT_acache_release(PINT_pinode *pinode)
{
    acache_debug("PINT_acache_release entered\n");
    assert(s_acache_initialized);
    if (pinode)
    {
        /*
          if we don't have the lock, acquire it; but one way or another,
          it better be locked below
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
    acache_debug("PINT_acache_release exited\n");
}

int PINT_acache_object_attr_deep_copy(
    PVFS_object_attr *dest,
    PVFS_object_attr *src)
{
    int ret = -1;

    if (dest && src)
    {
	if (src->mask & PVFS_ATTR_COMMON_UID)
        {
            dest->owner = src->owner;
        }
	if (src->mask & PVFS_ATTR_COMMON_GID)
        {
            dest->group = src->group;
        }
	if (src->mask & PVFS_ATTR_COMMON_PERM)
        {
            dest->perms = src->perms;
        }
	if (src->mask & PVFS_ATTR_COMMON_ATIME)
        {
            dest->atime = src->atime;
        }
	if (src->mask & PVFS_ATTR_COMMON_CTIME)
        {
            dest->ctime = src->ctime;
        }
        if (src->mask & PVFS_ATTR_COMMON_MTIME)
        {
            dest->mtime = src->mtime;
        }
	if (src->mask & PVFS_ATTR_COMMON_TYPE)
        {
            dest->objtype = src->objtype;
        }

        /*
          NOTE:
          we only copy the size out if we're actually a
          datafile object.  sometimes the size field is
          valid when the objtype is a metafile because
          of different uses of the acache.  In this case
          (namely, getattr), the size is stored in the
          acache before this deep copy, so it's okay
          that we're not copying here even though the
          size mask bit is set.

          if we don't do this trick, the metafile that
          caches the size will have it's union data
          overwritten with a bunk size.
        */
        if ((src->mask & PVFS_ATTR_DATA_SIZE) &&
            (src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_DATAFILE))
        {
            dest->u.data.size = src->u.data.size;
        }

	if ((src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_METAFILE) &&
            (src->mask & PVFS_ATTR_META_DFILES))
	{
	    PVFS_size df_array_size = src->u.meta.dfile_count *
                sizeof(PVFS_handle);

            if (df_array_size)
            {
		if ((dest->mask & PVFS_ATTR_META_DFILES) &&
		    dest->u.meta.dfile_count > 0)
                {
                    if (dest->u.meta.dfile_array)
                    {
                        free(dest->u.meta.dfile_array);
                    }
                }
		dest->u.meta.dfile_array = malloc(df_array_size);
		if (!dest->u.meta.dfile_array)
		{
                    return -ENOMEM;
		}
		memcpy(dest->u.meta.dfile_array,
                       src->u.meta.dfile_array, df_array_size);
	    } else {
		dest->u.meta.dfile_array = NULL;
	    }
	    dest->u.meta.dfile_count = src->u.meta.dfile_count;
	}

	if ((src->mask & PVFS_ATTR_COMMON_TYPE) &&
            (src->objtype == PVFS_TYPE_METAFILE) &&
            (src->mask & PVFS_ATTR_META_DIST))
	{
            assert(src->u.meta.dist_size > 0);

	    if ((dest->mask & PVFS_ATTR_META_DIST))
            {
                PINT_dist_free(dest->u.meta.dist);
            }
            dest->u.meta.dist = PINT_dist_copy(src->u.meta.dist);
            if (dest->u.meta.dist == NULL)
            {
                return -ENOMEM;
            }
            dest->u.meta.dist_size = src->u.meta.dist_size;
        }

        if (src->mask & PVFS_ATTR_SYMLNK_TARGET)
        {
            dest->u.sym.target_path_len = src->u.sym.target_path_len;
            dest->u.sym.target_path = strdup(src->u.sym.target_path);
            if (dest->u.sym.target_path == NULL)
            {
                return -ENOMEM;
            }
        }

	dest->mask = src->mask;
        ret = 0;
    }
    return ret;
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

/* free any internally allocated members; NOT passed in attr pointer */
void PINT_acache_object_attr_deep_free(PVFS_object_attr *attr)
{
    if (attr)
    {
        if (attr->objtype == PVFS_TYPE_METAFILE)
        {
            if (attr->mask & PVFS_ATTR_META_DFILES)
            {
                if (attr->u.meta.dfile_array)
                {
                    free(attr->u.meta.dfile_array);
                }
            }
            if (attr->mask & PVFS_ATTR_META_DIST)
            {
                if (attr->u.meta.dist)
                {
                    PINT_dist_free(attr->u.meta.dist);
                }
            }
        }
        else if (attr->objtype == PVFS_TYPE_SYMLINK)
        {
            if (attr->mask & PVFS_ATTR_SYMLNK_TARGET)
            {
                if ((attr->u.sym.target_path_len > 0) &&
                    attr->u.sym.target_path)
                {
                    free(attr->u.sym.target_path);
                }
            }
        }
    }
}

static int pinode_hash_refn(void *refn_p, int table_size)
{
    unsigned long tmp = 0;
    PVFS_object_ref *refn = (PVFS_object_ref *)refn_p;

    tmp += (unsigned long)(refn->handle + refn->fs_id);
    tmp = tmp%table_size;

    return ((int)tmp);
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
        return(1);
    }
    return(0);
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
    PINT_acache_object_attr_deep_free(&pinode->attr);
    gen_mutex_unlock(pinode->mutex);
    pinode_free(pinode);
    acache_debug("*** pinode_invalidate: freed pinode\n");
    acache_debug("pinode_invalidate exited\n");
}

static int pinode_status(PINT_pinode *pinode)
{
    struct timeval now;
    int ret = PINODE_STATUS_INVALID;

    acache_debug("pinode_status entered\n");

    /* if we don't have the lock, get it */
    gen_mutex_trylock(pinode->mutex);
    ret = pinode->status;
    if (ret == PINODE_STATUS_VALID)
    {
        ret = PINODE_STATUS_INVALID;
        if (pinode->ref_cnt > 0)
        {
            if (gettimeofday(&now, NULL) == 0)
            {
                ret = (((pinode->time_stamp.tv_sec < now.tv_sec) ||
                        ((pinode->time_stamp.tv_sec == now.tv_sec) &&
                         (pinode->time_stamp.tv_usec < now.tv_usec))) ?
                       PINODE_STATUS_EXPIRED : PINODE_STATUS_VALID);
            }
        }
    }
    acache_debug("PINODE STATUS IS ");
    switch(ret)
    {
        case PINODE_STATUS_VALID:
            acache_debug("PINODE STATUS VALID\n");
            break;
        case PINODE_STATUS_INVALID:
            acache_debug("PINODE STATUS INVALID\n");
            break;
        case PINODE_STATUS_EXPIRED:
            acache_debug("PINODE STATUS EXPIRED\n");
            break;
        default:
            acache_debug("UNKNOWN\n");
    }
    if (ret == PINODE_STATUS_EXPIRED)
    {
        pinode->ref_cnt--;
    }
    gen_mutex_unlock(pinode->mutex);
    acache_debug("pinode_status exited\n");
    return ret;
}

/* NOTE: called with the pinode mutex lock held */
static void pinode_update_timestamp(PINT_pinode **pinode)
{
    if (gettimeofday(&((*pinode)->time_stamp), NULL) == 0)
    {
        (*pinode)->time_stamp.tv_sec += (int)(s_acache_timeout_ms / 1000);
        (*pinode)->time_stamp.tv_usec +=
            (int)((s_acache_timeout_ms % 1000) * 1000);
        (*pinode)->ref_cnt++;
    }
    else
    {
        assert(0);
    }
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

                if (pinode_status(pinode) != PINODE_STATUS_VALID)
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
/*     fprintf(stderr,"reclaim_pinode_entries reclaimed %d entries\n", */
/*             num_reclaimed); */
/*     fprintf(stderr,"Total allocated is %d\n", */
/*             s_acache_allocated_entries); */
    acache_debug("reclaim_pinode_entries exited\n");
}
#endif /* PINT_ACACHE_AUTO_CLEANUP */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

