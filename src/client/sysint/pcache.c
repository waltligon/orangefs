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
#include "pint-sysint-utils.h"
#include "gen-locks.h"
#include "pcache.h"
#include "quickhash.h"

/* uncomment the following for verbose pcache debugging */
/* #define VERBOSE_PCACHE_DEBUG */

/*
  uncomment the following for an experimental pinode
  cleanup mechanism for trying to bound the number of
  pinode entries in the pcache at any given time
*/
/* #define PINT_PCACHE_AUTO_CLEANUP */


#ifdef VERBOSE_PCACHE_DEBUG
#define pcache_debug(x) gossip_debug(PCACHE_DEBUG,x)
#else
#define pcache_debug(...)
#endif

/*
  what we could do is disable the use of the pcache
  if locks are not available on the system, but for now...
*/
#ifndef __GEN_POSIX_LOCKING__
#error "Cannot use pcache without functioning mutex locking"
#endif

struct qhash_table *s_pcache_htable = NULL;
static gen_mutex_t *s_pcache_htable_mutex = NULL;

static int s_pcache_initialized = 0;
static int s_pcache_timeout_ms = (PINT_PCACHE_TIMEOUT * 1000);
static int s_pcache_allocated_entries = 0;

/* static internal helper methods */
static int pinode_hash_refn(void *pinode_refn_p, int table_size);
static int pinode_hash_refn_compare(void *key, struct qlist_head *link);
static PINT_pinode *pinode_alloc(void);
static void pinode_free(PINT_pinode *pinode);
static int pinode_status(PINT_pinode *pinode);
static void pinode_update_timestamp(PINT_pinode **pinode);
static void pinode_invalidate(PINT_pinode *pinode);

#ifdef PINT_PCACHE_AUTO_CLEANUP
static void reclaim_pinode_entries(void);
#endif


/*
  initializes pcache; MUST be called before
  using any other pcache methods.

  returns 0 on success; -1 otherwise
*/
int PINT_pcache_initialize()
{
    int ret = -1;

    pcache_debug("PINT_pcache_initialize entered\n");

    s_pcache_htable_mutex = gen_mutex_build();
    if (!s_pcache_htable_mutex)
    {
        return ret;
    }

    s_pcache_htable = qhash_init(
        pinode_hash_refn_compare,
        pinode_hash_refn, PINT_PCACHE_HTABLE_SIZE);
    assert(s_pcache_htable);
    if (!s_pcache_htable)
    {
        goto error_exit;
    }

    pcache_debug("PINT_pcache_initialize exiting\n");
    s_pcache_initialized = 1;
    return 0;

  error_exit:
    pcache_debug("PINT_pcache_initialize error exiting\n");
    PINT_pcache_finalize();
    return ret;
}

void PINT_pcache_finalize()
{
    int i = 0;
    PINT_pinode *pinode = NULL;
    struct qlist_head *link = NULL, *tmp = NULL;

    pcache_debug("PINT_pcache_finalize entered\n");
    if (s_pcache_htable_mutex && s_pcache_htable)
    {
        gen_mutex_lock(s_pcache_htable_mutex);
        for(i = 0; i < s_pcache_htable->table_size; i++)
        {
            qhash_for_each_safe(link, tmp, &(s_pcache_htable->array[i]))
            {
                pinode = qlist_entry(link, PINT_pinode, link);
                assert(pinode);

                pinode_free(pinode);
            }
        }
        gen_mutex_unlock(s_pcache_htable_mutex);
        gen_mutex_destroy(s_pcache_htable_mutex);
        qhash_finalize(s_pcache_htable);
    }

    /* make sure all pinodes are accounted for */
    assert(s_pcache_allocated_entries == 0);
    if (s_pcache_allocated_entries == 0)
    {
        pcache_debug("All pcache entries freed properly\n");
    }
    else
    {
        pcache_debug("Lost some pcache entries; memory leak\n");
    }

    s_pcache_htable = NULL;
    s_pcache_htable_mutex = NULL;
    pcache_debug("PINT_pcache_finalize exiting\n");
}

/*
  returns the pinode matching the specified reference if
  present in the pcache. returns NULL otherwise.

  NOTE: if a pinode is returned, it is returned with the
  lock held.  That means no one else can use it before
  the lock is released (in release, or set_valid);
*/
PINT_pinode *PINT_pcache_lookup(PVFS_pinode_reference refn)
{
    PINT_pinode *pinode = NULL;
    struct qhash_head *link = NULL;

    pcache_debug("PINT_pcache_lookup entered\n");
    assert(s_pcache_initialized);

    gen_mutex_lock(s_pcache_htable_mutex);
    link = qhash_search(s_pcache_htable, &refn);
    if (link)
    {
        pinode = qlist_entry(link, PINT_pinode, link);
        assert(pinode);
    }
    gen_mutex_unlock(s_pcache_htable_mutex);

    if (pinode)
    {
        gen_mutex_lock(pinode->mutex);
        assert(pinode->flag = PINODE_INTERNAL_FLAG_HASHED);
        pinode->ref_cnt++;
    }
    pcache_debug("PINT_pcache_lookup exiting\n");
    return pinode;
}

PINT_pinode *PINT_pcache_pinode_alloc()
{
#ifdef PINT_PCACHE_AUTO_CLEANUP
    /*
      PINT_PCACHE_NUM_FLUSH_ENTRIES is a soft limit that triggers
      an attempt to reclaim any expired or invalidated entries
      that currently reside in the pcache.
    */
    if (s_pcache_allocated_entries &&
        (s_pcache_allocated_entries % PINT_PCACHE_NUM_FLUSH_ENTRIES) == 0)
    {
        reclaim_pinode_entries();
    }
#endif
    return pinode_alloc();
}

void PINT_pcache_free_pinode(PINT_pinode *pinode)
{
    pinode_free(pinode);
}

int PINT_pcache_pinode_status(PINT_pinode *pinode)
{
    assert(s_pcache_initialized);
    pcache_debug("PINT_pcache_status called\n");
    return pinode_status(pinode);
}

void PINT_pcache_set_valid(PINT_pinode *pinode)
{
    pcache_debug("PINT_pcache_set_valid entered\n");
    assert(s_pcache_initialized);
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
            gen_mutex_lock(s_pcache_htable_mutex);
            qhash_add(s_pcache_htable, &pinode->refn, &pinode->link);
            gen_mutex_unlock(s_pcache_htable_mutex);

            pinode->flag = PINODE_INTERNAL_FLAG_HASHED;
            pcache_debug("*** added pinode to htable\n");
        }

        pinode->status = PINODE_STATUS_VALID;
        pinode_update_timestamp(&pinode);
        gen_mutex_unlock(pinode->mutex);
    }
    pcache_debug("PINT_pcache_set_valid exiting\n");
}

/* tries to release the pinode, based on the specified refn */
void PINT_pcache_invalidate(PVFS_pinode_reference refn)
{
    PINT_pinode *pinode = NULL;

    pcache_debug("PINT_pcache_invalidate entered\n");
    assert(s_pcache_initialized);

    pinode = PINT_pcache_lookup(refn);
    if (pinode)
    {
        /* drop the ref count we picked up in lookup */
        pinode->ref_cnt--;

        /* forcefully expire the entry */
        pinode->status = PINODE_STATUS_EXPIRED;

        /* we should have the lock at this point */
        gen_mutex_unlock(pinode->mutex);

        PINT_pcache_release(pinode);
    }
    pcache_debug("PINT_pcache_invalidate exiting\n");
}

void PINT_pcache_release_refn(PVFS_pinode_reference refn)
{
    PINT_pinode *pinode = PINT_pcache_lookup(refn);
    if (pinode)
    {
        /* drop the ref count we picked up in lookup */
        pinode->ref_cnt--;
        PINT_pcache_release(pinode);
    }
}

void PINT_pcache_release(PINT_pinode *pinode)
{
    pcache_debug("PINT_pcache_release entered\n");
    assert(s_pcache_initialized);
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
    pcache_debug("PINT_pcache_release exited\n");
}

int PINT_pcache_object_attr_deep_copy(
    PVFS_object_attr *dest,
    PVFS_object_attr *src)
{
    int ret = -1;
    PVFS_size df_array_size = 0;

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

        if (src->mask & PVFS_ATTR_DATA_SIZE)
        {
            assert(src->objtype == PVFS_TYPE_DATAFILE);

            dest->u.data.size = src->u.data.size;
        }

	if (src->mask & PVFS_ATTR_META_DFILES)
	{
            assert(src->objtype == PVFS_TYPE_METAFILE);

            dest->u.meta.dfile_array = NULL;
            dest->u.meta.dfile_count = src->u.meta.dfile_count;
            df_array_size = src->u.meta.dfile_count *
                sizeof(PVFS_handle);

            if (df_array_size)
            {
		if (dest->u.meta.dfile_array)
                {
                    free(dest->u.meta.dfile_array);
                }
		dest->u.meta.dfile_array =
                    (PVFS_handle *)malloc(df_array_size);
		if (!dest->u.meta.dfile_array)
		{
                    return -ENOMEM;
		}
		memcpy(dest->u.meta.dfile_array,
                       src->u.meta.dfile_array, df_array_size);
            }
	}

	if (src->mask & PVFS_ATTR_META_DIST)
	{
            assert(src->objtype == PVFS_TYPE_METAFILE);
            dest->u.meta.dist_size = src->u.meta.dist_size;

            if (dest->u.meta.dist)
            {
                PVFS_Dist_free(dest->u.meta.dist);
            }
            /*
              FIXME: Replace with PVFS_Dist_copy, which
              causes problems when used here
            */
            dest->u.meta.dist = (PVFS_Dist *)
                malloc(src->u.meta.dist_size);
            if (dest->u.meta.dist == NULL)
            {
                return -ENOMEM;
            }

            /* this encodes the previously decoded distribution into
             * our new space.
             */
            PINT_Dist_encode(dest->u.meta.dist, src->u.meta.dist);

            /* this does an in-place decoding of the distribution.  now
             * we have a decoded version where we want it.
             *
             * NOTE: we need to free this later.
             */
            PINT_Dist_decode(dest->u.meta.dist, NULL);
        }

        if (src->mask & PVFS_ATTR_SYMLNK_TARGET)
        {
            assert(src->objtype == PVFS_TYPE_SYMLINK);

            dest->u.sym.target_path_len = src->u.sym.target_path_len;
            dest->u.sym.target_path = strdup(src->u.sym.target_path);
            if (dest->u.sym.target_path == NULL)
            {
                return -ENOMEM;
            }
        }

	/* add mask to existing values */
	dest->mask |= src->mask;

        ret = 0;
    }
    return ret;
}

int PINT_pcache_get_timeout()
{
    assert(s_pcache_initialized);
    return s_pcache_timeout_ms;
}
void PINT_pcache_set_timeout(int max_timeout_ms)
{
    assert(s_pcache_initialized);
    s_pcache_timeout_ms = max_timeout_ms;
}

int PINT_pcache_get_size()
{
    return s_pcache_allocated_entries;
}

/* free any internally allocated members; NOT passed in attr pointer */
void PINT_pcache_object_attr_deep_free(PVFS_object_attr *attr)
{
    if (attr)
    {
        if (attr->mask & PVFS_ATTR_META_DFILES)
        {
            assert(attr->objtype == PVFS_TYPE_METAFILE);
            if (attr->u.meta.dfile_array)
            {
                free(attr->u.meta.dfile_array);
            }
        }
        if (attr->mask & PVFS_ATTR_META_DIST)
        {
            assert(attr->objtype == PVFS_TYPE_METAFILE);
            if (attr->u.meta.dist)
            {
                PVFS_Dist_free(attr->u.meta.dist);
            }
        }
        if (attr->mask & PVFS_ATTR_SYMLNK_TARGET)
        {
            assert(attr->objtype == PVFS_TYPE_SYMLINK);
            if ((attr->u.sym.target_path_len > 0) &&
                attr->u.sym.target_path)
            {
                free(attr->u.sym.target_path);
            }
        }
    }
}

static int pinode_hash_refn(void *pinode_refn_p, int table_size)
{
    unsigned long tmp = 0;
    PVFS_pinode_reference *refn = (PVFS_pinode_reference *)pinode_refn_p;

    tmp += (unsigned long)(refn->handle + refn->fs_id);
    tmp = tmp%table_size;

    return ((int)tmp);
}

static int pinode_hash_refn_compare(void *key, struct qlist_head *link)
{
    PINT_pinode *pinode = NULL;
    PVFS_pinode_reference *refn = (PVFS_pinode_reference *)key;

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

            s_pcache_allocated_entries++;
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
        s_pcache_allocated_entries--;
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

    pcache_debug("pinode_invalidate entered\n");
    gen_mutex_lock(pinode->mutex);
    pinode->status = PINODE_STATUS_INVALID;

    if (pinode->flag == PINODE_INTERNAL_FLAG_HASHED)
    {
        gen_mutex_lock(s_pcache_htable_mutex);
        link = qhash_search_and_remove(s_pcache_htable, &pinode->refn);
        if (link)
        {
            pinode = qlist_entry(link, PINT_pinode, link);
            assert(pinode);
            pinode->flag = PINODE_INTERNAL_FLAG_UNHASHED;
        }
        gen_mutex_unlock(s_pcache_htable_mutex);
        pcache_debug("*** pinode_invalidate: removed from htable\n");
    }
    PINT_pcache_object_attr_deep_free(&pinode->attr);
    gen_mutex_unlock(pinode->mutex);
    pinode_free(pinode);
    pcache_debug("*** pinode_invalidate: freed pinode\n");
    pcache_debug("pinode_invalidate exited\n");
}

static int pinode_status(PINT_pinode *pinode)
{
    struct timeval now;
    int ret = PINODE_STATUS_INVALID;

    pcache_debug("pinode_status entered\n");

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
    pcache_debug("PINODE STATUS IS ");
    switch(ret)
    {
        case PINODE_STATUS_VALID:
            pcache_debug("PINODE STATUS VALID\n");
            break;
        case PINODE_STATUS_INVALID:
            pcache_debug("PINODE STATUS INVALID\n");
            break;
        case PINODE_STATUS_EXPIRED:
            pcache_debug("PINODE STATUS EXPIRED\n");
            break;
        default:
            pcache_debug("UNKNOWN\n");
    }
    if (ret == PINODE_STATUS_EXPIRED)
    {
        pinode->ref_cnt--;
    }
    gen_mutex_unlock(pinode->mutex);
    pcache_debug("pinode_status exited\n");
    return ret;
}

/* NOTE: called with the pinode mutex lock held */
static void pinode_update_timestamp(PINT_pinode **pinode)
{
    if (gettimeofday(&((*pinode)->time_stamp), NULL) == 0)
    {
        (*pinode)->time_stamp.tv_sec += (int)(s_pcache_timeout_ms / 1000);
        (*pinode)->time_stamp.tv_usec +=
            (int)((s_pcache_timeout_ms % 1000) * 1000);
        (*pinode)->ref_cnt++;
    }
    else
    {
        assert(0);
    }
}

#ifdef PINT_PCACHE_AUTO_CLEANUP
/*
  attempts to reclaim up to PINT_PCACHE_NUM_FLUSH_ENTRIES
  stale pinode entries; may not reclaim any.
*/
static void reclaim_pinode_entries()
{
    PINT_pinode *pinode = NULL;
    struct qlist_head *link = NULL, *tmp = NULL;
    int i = 0, num_reclaimed = 0;

    pcache_debug("reclaim_pinode_entries entered\n");
    if (s_pcache_htable_mutex && s_pcache_htable)
    {
        gen_mutex_lock(s_pcache_htable_mutex);
        for(i = 0; i < s_pcache_htable->table_size; i++)
        {
            qhash_for_each_safe(link, tmp, &(s_pcache_htable->array[i]))
            {
                pinode = qlist_entry(link, PINT_pinode, link);
                assert(pinode);

                if (pinode_status(pinode) != PINODE_STATUS_VALID)
                {
                    pinode_invalidate(pinode);
                    if (num_reclaimed++ == PINT_PCACHE_NUM_FLUSH_ENTRIES)
                    {
                        goto reclaim_exit;
                    }
                }
            }
        }
      reclaim_exit:
        gen_mutex_unlock(s_pcache_htable_mutex);
    }
/*     fprintf(stderr,"reclaim_pinode_entries reclaimed %d entries\n", */
/*             num_reclaimed); */
/*     fprintf(stderr,"Total allocated is %d\n", */
/*             s_pcache_allocated_entries); */
    pcache_debug("reclaim_pinode_entries exited\n");
}
#endif /* PINT_PCACHE_AUTO_CLEANUP */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

