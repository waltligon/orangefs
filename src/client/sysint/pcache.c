/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#include <pcache.h>
#include <assert.h>

#define ECACHEFULL 1

#define PINT_ENABLE_PCACHE 1

/* define for more verbose pcache debugging output */
/* #define PCACHE_VERBOSE_DEBUG */

#if PINT_ENABLE_PCACHE
static int PINT_pcache_get_lru(void);
static int PINT_pcache_add_pinode(pinode *pnode);
static int check_expiry(pinode *pnode);
static void PINT_pcache_rotate_pinode(int item);
static void PINT_pcache_remove_element(int item);
static int PINT_pcache_update_pinode_timestamp(pinode* item);
static int PINT_pcache_pinode_release(pinode *pnode);

static pcache pvfs_pcache;
static int s_pint_pcache_timeout_ms = (PINT_PCACHE_TIMEOUT * 1000);

static inline int is_equal(
    PVFS_pinode_reference *p1,
    PVFS_pinode_reference *p2)
{
    return (p1 && p2 && (p1->handle == p2->handle) &&
            (p1->fs_id == p2->fs_id));
}

/* check_expiry
 *
 * check to determine if cached copy is stale based on timeout value
 *
 * returns PINODE_VALID on success, PINODE_EXPIRED on failure
 */
static inline int check_expiry(pinode *pnode)
{
    int ret = PINODE_EXPIRED;
    struct timeval cur;

    if (gettimeofday(&cur, NULL) == 0)
    {
        /* Does timestamp exceed the current time?
         * If yes, pinode is valid. If no, it is stale.
         */
        ret = (((pnode->tstamp.tv_sec > cur.tv_sec) ||
                ((pnode->tstamp.tv_sec == cur.tv_sec) &&
                 (pnode->tstamp.tv_usec > cur.tv_usec))) ?
               PINODE_VALID : PINODE_EXPIRED);
    }
    return ret;
}
#endif

/* PINT_pcache_pinode_release
 *
 * release a pinode: this decrements the reference count so we know if its safe
 * to deallocate this pinode when we need the least recently used item.
 *
 * returns 0 on success, -1 on failure
 */
static int PINT_pcache_pinode_release(pinode *pnode)
{
#if PINT_ENABLE_PCACHE
    int i;
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if (pvfs_pcache.element[i].pnode != NULL)
	{
            if (is_equal(&pnode->pinode_ref,
                         &pvfs_pcache.element[i].pnode->pinode_ref))
	    {
		pvfs_pcache.element[i].ref_count--;

		if ((pvfs_pcache.element[i].status ==
                     STATUS_SHOULD_DELETE) &&
                    (pvfs_pcache.element[i].ref_count == 0))
		{
		    PINT_pcache_remove_element(i);
		    PINT_pcache_pinode_dealloc(pvfs_pcache.element[i].pnode);
		}
		return 0;
	    }
	}
    }
    return -1;
#else
    return 0;
#endif
}

/* PINT_pcache_pinode_alloc 
 *
 * allocate a pinode 
 *
 * returns 0 on success, -1 on failure
 */
int PINT_pcache_pinode_alloc(pinode **pnode)
{
	*pnode = (pinode *)malloc(sizeof(pinode));
	if (!(*pnode))
        {
		return(-ENOMEM);
        }
	memset(*pnode,0,sizeof(pinode));	
	return(0);
}

/* PINT_pcache_pinode_dealloc 
 *
 * deallocate a pinode 
 *
 * returns nothing
 */
void PINT_pcache_pinode_dealloc(pinode *pnode)
{
    int i;

    if (pnode)
    {
	memset(pnode,0,sizeof(pinode));
	if (pnode->attr.objtype == PVFS_TYPE_METAFILE)
	{
	    gossip_ldebug(PCACHE_DEBUG, "METADATA file handles:\n");

	    for(i = 0; i < pnode->attr.u.meta.dfile_count; i++)
            {
		gossip_ldebug(PCACHE_DEBUG, "\t%Ld\n",
                              pnode->attr.u.meta.dfile_array[i]);
            }

	    if(pnode->attr.u.meta.dfile_array != NULL)
            {
		free(pnode->attr.u.meta.dfile_array);
            }
	}
	free(pnode);
    }
}

int PINT_pcache_get_timeout(void)
{
    return s_pint_pcache_timeout_ms;
}

void PINT_pcache_set_timeout(int max_timeout_ms)
{
    s_pint_pcache_timeout_ms = max_timeout_ms;
}

/* PINT_pcache_initialize
 *
 * initializes the cache
 *
 * returns 0 on success, -1 on failure
 */
int PINT_pcache_initialize(void)
{
#if PINT_ENABLE_PCACHE
	int i = 0;
	/* Init the mutex lock */
	pvfs_pcache.mt_lock = gen_mutex_build();
	pvfs_pcache.top = BAD_LINK;
	pvfs_pcache.bottom = 0;
	pvfs_pcache.count = 0;
	/* Form a link between cache elements */
	for(i = 0; i < PINT_PCACHE_MAX_ENTRIES; i++)
	{
		pvfs_pcache.element[i].prev = BAD_LINK;
		pvfs_pcache.element[i].next = BAD_LINK;
		pvfs_pcache.element[i].status = STATUS_UNUSED;
		pvfs_pcache.element[i].pnode = NULL;
		pvfs_pcache.element[i].ref_count = 0;
	}
#endif
	return(0);
}

/* PINT_pcache_finalize
 *
 * deallocates memory as needed
 *
 * returns 0 on success, -1 on failure
 */
int PINT_pcache_finalize(void)
{
#if PINT_ENABLE_PCACHE
	int i = 0;

	/* Grab the mutex - do we need to do this? */
	gen_mutex_lock(pvfs_pcache.mt_lock);

	/* Deallocate the pinodes */
	for(i = 0; i < PINT_PCACHE_MAX_ENTRIES; i++)
	{
            if (pvfs_pcache.element[i].pnode)
            {
		PINT_pcache_pinode_dealloc(pvfs_pcache.element[i].pnode);
            }
	}
	/* TODO: does a mutex need to go here?*/
	/* Release the mutex */
	gen_mutex_unlock(pvfs_pcache.mt_lock);
	
	/* Destroy the mutex */
	gen_mutex_destroy(pvfs_pcache.mt_lock);
	
#endif
	return(0);
}

/* PINT_pcache_insert
 *
 * adds/merges an element to the pinode cache
 *
 * returns 0 on success, -1 on error
 */
int PINT_pcache_insert(pinode *pnode)
{
#if PINT_ENABLE_PCACHE
    int i = 0,entry = 0;
    int ret = 0;
    unsigned char entry_found = 0;

    if (pnode == NULL)
    {
	return (-EINVAL);
    }

    gen_mutex_lock(pvfs_pcache.mt_lock);
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if (pvfs_pcache.element[i].status == STATUS_USED)
	{
            if (is_equal(&pnode->pinode_ref,
                         &pvfs_pcache.element[i].pnode->pinode_ref))
	    {
		gossip_ldebug(
                    PCACHE_DEBUG, "CACHE ADDRESS: %d ARGUMENT ADDRESS: "
                    "%d.\n", (int)pvfs_pcache.element[i].pnode,
                    (int)pnode);
		entry_found = 1;
		entry = i;
		break;
	    }
	}
    }

    if (!entry_found)
    {
	ret = PINT_pcache_add_pinode(pnode);
	if (ret < 0)
	{
	    /* right now the only reason why we wouldn't be able to add 
	     * something to the cache is b/c its full (all slots have an entry
	     * with a reference count greater than 1)
	     */
	    return -1;
	}
    }
    else
    {
	gossip_ldebug(
            PCACHE_DEBUG, "pinode %Ld %d exists in cache at pos %d.\n",
            pnode->pinode_ref.handle, pnode->pinode_ref.fs_id, entry);

	/* Element present in the cache, merge it.
	 * Pinode with same <handle,fs_id> found...
	 * Now, we need to merge the pinode values with
	 * the older one 
	 */
	pvfs_pcache.element[entry].ref_count++;
        pvfs_pcache.element[entry].pnode = pnode;
	PINT_pcache_rotate_pinode(entry);
	PINT_pcache_update_pinode_timestamp(pvfs_pcache.element[entry].pnode);
    }
    gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif
    return(0);
}

/* PINT_pcache_insert
 *
 * this decrements the reference count after a process is done with the pinode
 * that it just inserted.
 *
 * returns 0 on success, -1 on error
 */
int PINT_pcache_insert_rls(pinode *pnode)
{
    return PINT_pcache_pinode_release(pnode);
}

/* PINT_pcache_lookup
 *
 * find the element in the cache
 *
 * returns PCACHE_LOOKUP_FAILURE on failure and
 * returns PCACHE_LOOKUP_SUCCESS on success
 */
int PINT_pcache_lookup(PVFS_pinode_reference refn, pinode **pinode_ptr)
{
    int ret = PCACHE_LOOKUP_FAILURE;
#if PINT_ENABLE_PCACHE
    int i = 0;

    gen_mutex_lock(pvfs_pcache.mt_lock);
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
        if (is_equal(&refn, &pvfs_pcache.element[i].pnode->pinode_ref))
	{
	    /* we don't want old pinodes */
	    if (check_expiry(pvfs_pcache.element[i].pnode) == PINODE_VALID)
	    {
		pvfs_pcache.element[i].ref_count++;
		*pinode_ptr = pvfs_pcache.element[i].pnode;
		PINT_pcache_rotate_pinode(i);
		ret = PCACHE_LOOKUP_SUCCESS;
		break;
	    }
	}
    }
    gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif
    return ret;
}

/* PINT_pcache_lookup_rls
 *
 * this decrements the reference count after a process is done with
 * the pinode that it looked up
 */
int PINT_pcache_lookup_rls(pinode *pinode_ptr)
{
    return PINT_pcache_pinode_release(pinode_ptr);
}

/* PINT_pcache_remove
 *
 * remove an element from the pinode cache
 *
 * returns 0 on success, -1 on failure
 *
 * Special case: if someone hasn't released this pinode yet, we don't want to 
 * remove it, so we set a flag for the release function to remove it when the
 * last process is done using it.
 *
 */
int PINT_pcache_remove(PVFS_pinode_reference refn, pinode **item)
{
#if PINT_ENABLE_PCACHE
    int i = 0;

    /* No match found */
    *item = NULL;

#ifdef PCACHE_VERBOSE_DEBUG
    gossip_ldebug(
        PCACHE_DEBUG, "PINT_pcache_remove( %lld %d ) top: %d bottom: "
        "%d count: %d\n", refn.handle, refn.fs_id, pvfs_pcache.top,
        pvfs_pcache.bottom, pvfs_pcache.count);
#endif

    gen_mutex_lock(pvfs_pcache.mt_lock);
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if (pvfs_pcache.element[i].pnode != NULL)
	{
            if (is_equal(&refn, &pvfs_pcache.element[i].pnode->pinode_ref))
	    {
		/* don't delete if somebody else is looking at this copy too */
		if (pvfs_pcache.element[i].ref_count != 1)
		{
		    *item = pvfs_pcache.element[i].pnode;
		    PINT_pcache_remove_element(i);
		}
		else
		{
                    /* trying to remove an item someone hasn't released */
		    pvfs_pcache.element[i].status = STATUS_SHOULD_DELETE;
                    gen_mutex_unlock(pvfs_pcache.mt_lock);
		    return -1;
		}
	    }
	}
    }
    gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif
    return(0);
}

void PINT_pcache_flush_reference(PVFS_pinode_reference refn)
{
#if PINT_ENABLE_PCACHE
    int i = 0;

    gen_mutex_lock(pvfs_pcache.mt_lock);
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if (pvfs_pcache.element[i].pnode != NULL)
	{
            if (is_equal(&refn, &pvfs_pcache.element[i].pnode->pinode_ref))
	    {
		/* don't delete if somebody else is looking at this copy too */
		if (pvfs_pcache.element[i].ref_count != 1)
		{
		    PINT_pcache_remove_element(i);
		}
		else
		{
                    /* trying to remove an item someone hasn't released */
		    pvfs_pcache.element[i].status = STATUS_SHOULD_DELETE;
                    break;
		}
	    }
	}
    }
    gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif
}


/* from this point on, its all static helper functions */
#if PINT_ENABLE_PCACHE

/* PINT_pcache_remove_element
 *
 * unlinks a pinode from the list
 *
 * no return value
 */
static void PINT_pcache_remove_element(int item)
{
    int prev = 0,next = 0;

    pvfs_pcache.element[item].status = STATUS_UNUSED;
    pvfs_pcache.element[item].ref_count = 0;

    /*PINT_pcache_pinode_dealloc(pvfs_pcache.element[item].pnode);*/
    pvfs_pcache.element[item].pnode = NULL;
    pvfs_pcache.count--;

    /* if there's exactly one item in the list, just get rid of it*/
    if (pvfs_pcache.top == pvfs_pcache.bottom)
    {
        pvfs_pcache.top = 0;
        pvfs_pcache.bottom = 0;
        pvfs_pcache.element[item].prev = BAD_LINK;
        pvfs_pcache.element[item].next = BAD_LINK;
        return;
    }

    /* depending on where the pinode is in the list, we have to do different
     * things if its the first, last, or somewhere in the middle.
     */

    if (item == pvfs_pcache.top)
    {
        /* Adjust top */
        pvfs_pcache.top = pvfs_pcache.element[item].next;
        pvfs_pcache.element[pvfs_pcache.top].prev = BAD_LINK;
    }
    else if (item == pvfs_pcache.bottom)
    {
        /* Adjust bottom */
        pvfs_pcache.bottom = pvfs_pcache.element[item].prev;
        pvfs_pcache.element[pvfs_pcache.bottom].next = -1;
    }
    else
    {
        /* Item in the middle */
        prev = pvfs_pcache.element[item].prev;
        next = pvfs_pcache.element[item].next;
        pvfs_pcache.element[prev].next = next;
        pvfs_pcache.element[next].prev = prev;
    }
}

/* PINT_pcache_get_lru
 *
 * implements cache replacement policy(LRU)
 *
 * returns 0 on success, -errno on failure
 */
static int PINT_pcache_get_lru(void)
{
    int new = 0, i = 0, prev = 0, next = 0, found_one = 0;

    if (pvfs_pcache.count == PINT_PCACHE_MAX_ENTRIES)
    {
	/* this isn't a straight "least recently used" implementation b/c we
	 * have to deal with multiple processes using the pinode structures
	 * so if the real "least recently used" item still hasn't been released
	 * then we need to go to the "next least recently used"
	 * in the worst case (every entry in the cache is still referenced)
	 * we're running an O(N) operation.... is there a way to make this
	 * better?
	 */
	for(new = pvfs_pcache.bottom;
            new != BAD_LINK; new = pvfs_pcache.element[new].prev)
	{
	    if (pvfs_pcache.element[new].ref_count == 0)
	    {
		found_one = 1;
		/* we're either at the bottom, in the middle somewhere, or
		 * on the last entry (the top) */
		if (new == pvfs_pcache.bottom)
		{
#ifdef PCACHE_VERBOSE_DEBUG
		    gossip_ldebug(PCACHE_DEBUG, "lru item found at "
                                  "pcache.bottom: %d.\n", new);
#endif
		    /* last entry in list */
		    pvfs_pcache.bottom = pvfs_pcache.element[new].prev;
		    pvfs_pcache.element[pvfs_pcache.bottom].next = BAD_LINK;
		}
		else if (new == pvfs_pcache.top)
		{
#ifdef PCACHE_VERBOSE_DEBUG
		    gossip_ldebug(PCACHE_DEBUG, "lru item found at "
                                  "pcache.top: %d.\n", new);
#endif
		    /* first entry in list */
		    pvfs_pcache.top = pvfs_pcache.element[new].next;
		    pvfs_pcache.element[pvfs_pcache.top].prev = BAD_LINK;
		}
		else
		{
#ifdef PCACHE_VERBOSE_DEBUG
		    gossip_ldebug(PCACHE_DEBUG,
                                  "lru item found at: %d.\n", new);
#endif
		    /* somewhere in the middle */
		    prev = pvfs_pcache.element[new].prev;
		    next = pvfs_pcache.element[new].next;
		    pvfs_pcache.element[prev].next = next;
		    pvfs_pcache.element[next].prev = prev;
		}
		break;
	    }
	}

	if (found_one)
	{
#ifdef PCACHE_VERBOSE_DEBUG
	    gossip_ldebug(PCACHE_DEBUG, "deallocating pinode %lld %d.\n",
                          pvfs_pcache.element[next].pnode->pinode_ref.handle,
                          pvfs_pcache.element[next].pnode->pinode_ref.fs_id);
#endif
	    PINT_pcache_pinode_dealloc(pvfs_pcache.element[new].pnode);
	    return new;
	}
	else
	{
#ifdef PCACHE_VERBOSE_DEBUG
	    gossip_ldebug(
                PCACHE_DEBUG, "unable to get lru pinode b/c all entries "
                "have a ref_count greater than 1.\n");
#endif
            /* should probbably be -ECACHEISFULL or something */
	    return BAD_LINK;
	}
    }
    else
    {
	for(i = 0; i < PINT_PCACHE_MAX_ENTRIES; i++)
	{
	    if (pvfs_pcache.element[i].status == STATUS_UNUSED)
	    {
#ifdef PCACHE_VERBOSE_DEBUG
		gossip_ldebug(PCACHE_DEBUG, "pcache.count: %d new pinode "
                              "placecd at %d\n",pvfs_pcache.count, i);
#endif
 		pvfs_pcache.count++;
		return i;
	    }
	}
    }

    gossip_ldebug(PCACHE_DEBUG, "error getting least recently used pinode.\n");
    assert(0);
}

/* PINT_pcache_rotate_pinode()
 *
 * moves the specified item to the top of the dcache linked list to prevent it
 * from being identified as the least recently used item in the cache.
 *
 * no return value
 */
static void PINT_pcache_rotate_pinode(int item)
{
    int prev = 0, next = 0, new_bottom;

#ifdef PCACHE_VERBOSE_DEBUG
    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_rotate_pinode( %d ) top: %d "
                  "bottom: %d count: %d prev: %d next: %d\n",item,
                  pvfs_pcache.top, pvfs_pcache.bottom, pvfs_pcache.count,
                  pvfs_pcache.element[item].prev,
                  pvfs_pcache.element[item].next);
#endif

    if (pvfs_pcache.top != pvfs_pcache.bottom) 
    {
	if (pvfs_pcache.top != item)
	{
	    /* only move links if there's more than one thing in the list
	     * or we're not already at the top
	     */

	    if (pvfs_pcache.bottom == item)
	    {
		new_bottom = pvfs_pcache.element[pvfs_pcache.bottom].prev;

		pvfs_pcache.element[new_bottom].next = BAD_LINK;
		pvfs_pcache.bottom = new_bottom;
	    }
	    else
	    {
		/*somewhere in the middle*/
		next = pvfs_pcache.element[item].next;
		prev = pvfs_pcache.element[item].prev;

		pvfs_pcache.element[prev].next = next;
		pvfs_pcache.element[next].prev = prev;
	    }

	    pvfs_pcache.element[pvfs_pcache.top].prev = item;

	    pvfs_pcache.element[item].next = pvfs_pcache.top;
	    pvfs_pcache.element[item].prev = BAD_LINK;
	    pvfs_pcache.top = item;
	}
    }
}


/* PINT_pcache_add_pinode
 *
 * adds pinode to cache and updates cache variables
 *
 * returns 0 on success
 */
static int PINT_pcache_add_pinode(pinode *pnode)
{
    int new = 0;

    new = PINT_pcache_get_lru();
    if (new == BAD_LINK)
    {
	return -ECACHEFULL;
    }

    /* Adding the element to the cache */
    pvfs_pcache.element[new].status = STATUS_USED;
    pvfs_pcache.element[new].ref_count++;
    pvfs_pcache.element[new].pnode = pnode;
    pvfs_pcache.element[new].prev = BAD_LINK;
    pvfs_pcache.element[new].next = pvfs_pcache.top;

    /* Make previous element point to new entry */
    if (pvfs_pcache.top != BAD_LINK)
    {
	pvfs_pcache.element[pvfs_pcache.top].prev = new;
    }

    /* Readjust the top */
    pvfs_pcache.top = new;

    PINT_pcache_update_pinode_timestamp(pvfs_pcache.element[new].pnode);

    return(0);
}

static int PINT_pcache_update_pinode_timestamp(pinode* item)
{
    int ret = 0;

    ret = gettimeofday(&item->tstamp,NULL);
    if (ret == 0)
    {
        item->tstamp.tv_sec +=
            (int)(s_pint_pcache_timeout_ms / 1000);
        item->tstamp.tv_usec +=
            (int)((s_pint_pcache_timeout_ms % 1000) * 1000);
    }
    return ret;
}

/* similar to pinode-helper.c:phelper_fill_attr */
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
            dest->u.data.size = src->u.data.size;
        }

	if (src->mask & PVFS_ATTR_META_DFILES)
	{
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
			return(-ENOMEM);
		}
		memcpy(dest->u.meta.dfile_array,
                       src->u.meta.dfile_array, df_array_size);
            }
	}

	if (src->mask & PVFS_ATTR_META_DIST)
	{
            dest->u.meta.dist_size = src->u.meta.dist_size;

            gossip_lerr("WARNING: packing distribution to memcpy it.\n");
            if (dest->u.meta.dist)
            {
                /* FIXME: memory leak */
                gossip_lerr("WARNING: need to free old dist, "
                            "but I don't know how.\n");
            }
            dest->u.meta.dist = malloc(src->u.meta.dist_size);
            if(dest->u.meta.dist == NULL)
            {
                return(-ENOMEM);
            }
            PINT_Dist_encode(dest->u.meta.dist, src->u.meta.dist);
            PINT_Dist_decode(dest->u.meta.dist, NULL);
        }

	/* add mask to existing values */
	dest->mask |= src->mask;

        ret = 0;
    }
    return ret;
}

/*
  given a meta_attr and fs_id, pull out all data attrs from the
  pcache that we can and fill in out_attrs by making the array
  point to each entry's attr object.  these should not be freed
  by the caller.

  returns 0 if all datafiles were pulled from the pcache;
  return 1 otherwise
*/
int PINT_pcache_retrieve_datafile_attrs(
    PVFS_object_attr meta_attr,
    PVFS_fs_id fs_id,
    PVFS_object_attr **out_attrs,
    int *in_out_num_attrs)
{
    int ret = 1;
#if PINT_ENABLE_PCACHE
    int i = 0, limit = 0;
    PINT_pinode *pinode = NULL;
    PVFS_pinode_reference refn;

    if (out_attrs && in_out_num_attrs && (*in_out_num_attrs))
    {
        limit = *in_out_num_attrs;
        *in_out_num_attrs = 0;

        assert(meta_attr.objtype == PVFS_TYPE_METAFILE);

        refn.fs_id = fs_id;
        for(i = 0; i < meta_attr.u.meta.dfile_count; i++)
        {
            refn.handle = meta_attr.u.meta.dfile_array[i];
            if (PINT_pcache_lookup(refn, &pinode) == PCACHE_LOOKUP_FAILURE)
            {
                break;
            }
            else
            {
                out_attrs[i] = &pinode->attr;
            }

            if (i == limit)
            {
                break;
            }
        }
        *in_out_num_attrs = i;

        ret = (((i == limit) ||
                (i == meta_attr.u.meta.dfile_count)) ? 0 : 1);
    }
#endif
    return ret;
}

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

