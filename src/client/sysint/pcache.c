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

#if PINT_ENABLE_PCACHE
static int PINT_pcache_get_lru(void);
static int PINT_pcache_add_pinode(pinode *pnode);
static void PINT_pcache_merge_pinode(pinode *p1,pinode *p2);
static int check_expiry(pinode *pnode);
static void PINT_pcache_rotate_pinode(int item);
static void PINT_pcache_remove_element(int item);
static int PINT_pcache_update_pinode_timestamp(pinode* item);
static int PINT_pcache_pinode_release(pinode *pnode);

static pcache pvfs_pcache;
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
	    if ((pnode->pinode_ref.handle ==
		pvfs_pcache.element[i].pnode->pinode_ref.handle)
		    && (pnode->pinode_ref.fs_id ==
			pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
	    {
		pvfs_pcache.element[i].ref_count--;
		if ((pvfs_pcache.element[i].status == STATUS_SHOULD_DELETE) && (pvfs_pcache.element[i].ref_count == 0))
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
		return(-ENOMEM);

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
    if (pnode != NULL)
    {
	memset(pnode,0,sizeof(pinode));
	if (pnode->attr.objtype == PVFS_TYPE_METAFILE)
	{
	    gossip_ldebug(PCACHE_DEBUG, "METADATA file handles:\n");
	    for(i = 0; i < pnode->attr.u.meta.nr_datafiles; i++)
		gossip_ldebug(PCACHE_DEBUG, "\t%lld\n", pnode->attr.u.meta.dfh[i]);
	    if(pnode->attr.u.meta.dfh != NULL)
		free(pnode->attr.u.meta.dfh);
	}
	free(pnode);
    }
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
		PINT_pcache_pinode_dealloc(pvfs_pcache.element[i].pnode);
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
int PINT_pcache_insert(pinode *pnode )
{
#if PINT_ENABLE_PCACHE
    int i = 0,entry = 0;
    int ret = 0;
    unsigned char entry_found = 0;

    /* don't pass in null pointers kthnx */
    if (pnode == NULL)
    {
	return (-EINVAL);
    }

    gossip_ldebug(PCACHE_DEBUG, "inserting pinode %lld %d.\n", pnode->pinode_ref.handle, pnode->pinode_ref.fs_id);

    /* Grab the mutex */
    gen_mutex_lock(pvfs_pcache.mt_lock);
	
    /* Search the cache */
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if (pvfs_pcache.element[i].status == STATUS_USED)
	{
	    if ((pnode->pinode_ref.handle ==
		pvfs_pcache.element[i].pnode->pinode_ref.handle)
		    && (pnode->pinode_ref.fs_id ==
			pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
	    {
		gossip_ldebug(PCACHE_DEBUG, "CACHE ADDRESS: %d ARGUMENT ADDRESS: %d.\n", (int)pvfs_pcache.element[i].pnode, (int)pnode);
		entry_found = 1;
		entry = i;
		break;
	    }
	}
    }

    /* Add/Merge element to the cache */
    if (!entry_found)
    {
	/* Element absent in cache, add it */
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
	gossip_ldebug(PCACHE_DEBUG, "pinode %lld %d exists in cache at pos %d.\n", pnode->pinode_ref.handle, pnode->pinode_ref.fs_id, entry);
	/* Element present in the cache, merge it */
	/* Pinode with same <handle,fs_id> found...
	 * Now, we need to merge the pinode values with
	 * the older one 
	 */
	pvfs_pcache.element[entry].ref_count++;
	PINT_pcache_merge_pinode(pvfs_pcache.element[entry].pnode,pnode);
	PINT_pcache_rotate_pinode(entry);
	PINT_pcache_update_pinode_timestamp(pvfs_pcache.element[entry].pnode);
    }


    /* Release the mutex */
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
int PINT_pcache_insert_rls(pinode *pnode )
{
    return PINT_pcache_pinode_release(pnode);
}

/* PINT_pcache_lookup
 *
 * find the element in the cache
 *
 * returns PCACHE_LOOKUP_FAILURE on failure and PCACHE_LOOKUP_SUCCESS on success 
 */
int PINT_pcache_lookup(pinode_reference refn,pinode **pinode_ptr)
{
    int ret = PCACHE_LOOKUP_FAILURE;
#if PINT_ENABLE_PCACHE
    int i = 0;

    /* Grab a mutex */
    gen_mutex_lock(pvfs_pcache.mt_lock);
    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_lookup( %lld %d )\n", refn.handle, refn.fs_id);

    /* Search the cache */
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_lookup(_) top: %d bottom: %d element: %d\n", pvfs_pcache.top, pvfs_pcache.bottom, i);
	if ((refn.handle == pvfs_pcache.element[i].pnode->pinode_ref.handle)
	    && (refn.fs_id == pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
	{
	    /* we don't want old pinodes */
	    if (check_expiry(pvfs_pcache.element[i].pnode) == PINODE_VALID)
	    {
		pvfs_pcache.element[i].ref_count++;
		(*pinode_ptr) = pvfs_pcache.element[i].pnode;
		PINT_pcache_rotate_pinode(i);
		ret = PCACHE_LOOKUP_SUCCESS;
		break;
		gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_lookup() item found at position: %d.\n", i);
	    }
	}
    }

    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_lookup(_) returning\n");

    /* Release the mutex */
    gen_mutex_unlock(pvfs_pcache.mt_lock);

#endif
    return ret;
}

/* PINT_pcache_lookup_rls
 *
 * this decrements the reference count after a process is done with the pinode
 * that it looked up
 *
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
int PINT_pcache_remove(pinode_reference refn,pinode **item)
{
#if PINT_ENABLE_PCACHE
    int i = 0;

    /* Grab a mutex */
    gen_mutex_lock(pvfs_pcache.mt_lock);

    /* No match found */
    *item = NULL;

    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_remove( %lld %d ) top: %d bottom: %d count: %d\n", refn.handle, refn.fs_id, pvfs_pcache.top, pvfs_pcache.bottom, pvfs_pcache.count);
    /* Search the cache */
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if (pvfs_pcache.element[i].pnode != NULL)
	{
	    //gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_remove(_) element %d\n", i);
	    if ((refn.handle == pvfs_pcache.element[i].pnode->pinode_ref.handle)
	      && (refn.fs_id == pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
	    {
		/* don't delete if somebody else is looking at this copy too */
		if (pvfs_pcache.element[i].ref_count != 1)
		{
		    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_remove(_) item found in cache at pos %d\n", i);
		    *item = pvfs_pcache.element[i].pnode;
		    PINT_pcache_remove_element(i);
		}
		else
		{
		    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_remove(_) trying to remove an item someone hasn't released %d\n", i);
		    pvfs_pcache.element[i].status = STATUS_SHOULD_DELETE;
		    return -1;
		}
	    }
	}
    }

    /* Release the mutex */
    gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif
    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_remove(_) returning\n");
    return(0);
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
	for(new = pvfs_pcache.bottom; new != BAD_LINK; new = pvfs_pcache.element[new].prev)
	{
	    if (pvfs_pcache.element[new].ref_count == 0)
	    {
		found_one = 1;
		/* we're either at the bottom, in the middle somewhere, or
		 * on the last entry (the top) */
		if (new == pvfs_pcache.bottom)
		{
		    /* last entry in list */
		    pvfs_pcache.bottom = pvfs_pcache.element[new].prev;
		    pvfs_pcache.element[pvfs_pcache.bottom].next = BAD_LINK;
		    gossip_ldebug(PCACHE_DEBUG, "lru item found at pcache.bottom: %d.\n", new);
		}
		else if (new == pvfs_pcache.top)
		{
		    /* first entry in list */
		    pvfs_pcache.top = pvfs_pcache.element[new].next;
		    pvfs_pcache.element[pvfs_pcache.top].prev = BAD_LINK;
		    gossip_ldebug(PCACHE_DEBUG, "lru item found at pcache.top: %d.\n", new);
		}
		else
		{
		    gossip_ldebug(PCACHE_DEBUG, "lru item found at: %d.\n", new);
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
	    gossip_ldebug(PCACHE_DEBUG, "deallocating pinode %lld %d.\n", pvfs_pcache.element[next].pnode->pinode_ref.handle, pvfs_pcache.element[next].pnode->pinode_ref.fs_id);
	    PINT_pcache_pinode_dealloc(pvfs_pcache.element[new].pnode);
	    return new;
	}
	else
	{
	    gossip_ldebug(PCACHE_DEBUG, "unable to get lru pinode b/c all entries have a ref_count greater than 1.\n");
	    return BAD_LINK;/* should probbably be -ECACHEISFULL or something */
	}
    }
    else
    {
	for(i = 0; i < PINT_PCACHE_MAX_ENTRIES; i++)
	{
	    if (pvfs_pcache.element[i].status == STATUS_UNUSED)
	    {
		gossip_ldebug(PCACHE_DEBUG, "pcache.count: %d new pinode placecd at %d\n",pvfs_pcache.count, i);
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
    gossip_ldebug(PCACHE_DEBUG, "PINT_pcache_rotate_pinode( %d ) top: %d bottom: %d count: %d prev: %d next: %d\n",item, pvfs_pcache.top, pvfs_pcache.bottom, pvfs_pcache.count, pvfs_pcache.element[item].prev, pvfs_pcache.element[item].next);
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


/* PINT_pcache_merge_pinode
 *
 * merges pinodes based on timestamps
 *
 * returns nothing
 */
static void PINT_pcache_merge_pinode(pinode *p1,pinode *p2)
{
	/* Check the attribute timestamps to see which
	 * pinode is the latest 
	 */
	if (p1->tstamp.tv_sec > p2->tstamp.tv_sec ||
			(p1->tstamp.tv_sec == p2->tstamp.tv_sec &&
			 p1->tstamp.tv_usec > p2->tstamp.tv_usec))
	{
		p2->attr = p1->attr;
	}

	/*TODO: when merging pinodes, what happens to the size? */
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

    /* Get the free item */
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
	pvfs_pcache.element[pvfs_pcache.top].prev = new;
    /* Readjust the top */
    pvfs_pcache.top = new;

    PINT_pcache_update_pinode_timestamp(pvfs_pcache.element[new].pnode);

    return(0);
}

/* check_expiry
 *
 * check to determine if cached copy is stale based on timeout value
 *
 * returns PINODE_VALID on success, PINODE_EXPIRED on failure
 */
static int check_expiry(pinode *pnode)
{
    int ret;
    struct timeval cur;
    ret = gettimeofday(&cur, NULL);
    if (ret < 0)
    {
	return PINODE_EXPIRED;
    }

    /* Does timestamp exceed the current time?
     * If yes, pinode is valid. If no, it is stale.
     */
    if (pnode->tstamp.tv_sec > cur.tv_sec || (pnode->tstamp.tv_sec == cur.tv_sec 
	&& pnode->tstamp.tv_usec > cur.tv_usec))
    {
	return PINODE_VALID;
    }

    return PINODE_EXPIRED;
}

static int PINT_pcache_update_pinode_timestamp(pinode* item)
{
    int ret = 0;

    /* Update the timestamp */
    ret = gettimeofday(&item->tstamp,NULL);
    if (ret < 0)
    {
        return(-1);
    }

    item->tstamp.tv_sec += PINT_PCACHE_TIMEOUT;

    return(0);
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

