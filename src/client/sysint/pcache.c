/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#include <pcache.h>
#include <assert.h>

#define PINT_ENABLE_PCACHE 0

#if PINT_ENABLE_PCACHE
static int PINT_pcache_get_lru(void);
static int PINT_pcache_add_pinode(pinode *pnode);
static void PINT_pcache_merge_pinode(pinode *p1,pinode *p2);
static int check_expiry(pinode *pnode);
static void PINT_pcache_rotate_pinode(int item);
static void PINT_pcache_remove_element(int item);

static pcache pvfs_pcache;
#endif

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
	if ((pnode->attr.objtype & ATTR_META) == ATTR_META)
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
	
    /* Grab the mutex */
    gen_mutex_lock(pvfs_pcache.mt_lock);
	
    /* Search the cache */
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if (pvfs_pcache.element[i].pnode != NULL)
	{
	    if ((pnode->pinode_ref.handle ==
		pvfs_pcache.element[i].pnode->pinode_ref.handle)
		    && (pnode->pinode_ref.fs_id ==
			pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
	    {
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
    }
    else
    {
	/* Element present in the cache, merge it */
	/* Pinode with same <handle,fs_id> found...
	 * Now, we need to merge the pinode values with
	 * the older one 
	 */
	PINT_pcache_merge_pinode(pvfs_pcache.element[entry].pnode,pnode);
	PINT_pcache_rotate_pinode(entry);
    }

    /* Release the mutex */
    gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif
    return(0);
}

/* PINT_pcache_lookup
 *
 * find the element in the cache
 *
 * returns PCACHE_LOOKUP_FAILURE on failure and PCACHE_LOOKUP_SUCCESS on success 
 */
int PINT_pcache_lookup(pinode_reference refn,pinode **pinode_ptr)
{
    int ret;
#if PINT_ENABLE_PCACHE
    int i = 0;

    /* Grab a mutex */
    gen_mutex_lock(pvfs_pcache.mt_lock);

    /* No match found */
    ret = PCACHE_LOOKUP_FAILURE;
	
    /* Search the cache */
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if ((refn.handle == pvfs_pcache.element[i].pnode->pinode_ref.handle)
	    && (refn.fs_id == pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
	{
	    /* we don't want old pinodes */
	    if (check_expiry(pvfs_pcache.element[i].pnode) == PINODE_VALID)
	    {
		(*pinode_ptr) = pvfs_pcache.element[i].pnode;
		PINT_pcache_rotate_pinode(i);
		ret = PCACHE_LOOKUP_SUCCESS;
	    }
	}
    }

    /* Release the mutex */
    gen_mutex_unlock(pvfs_pcache.mt_lock);

#else
    /* No match found */
    *pinode_ptr = NULL;
    ret = PCACHE_LOOKUP_FAILURE;
#endif
    return ret;
}

/* PINT_pcache_remove
 *
 * remove an element from the pinode cache
 *
 * returns 0 on success, -1 on failure
 */
int PINT_pcache_remove(pinode_reference refn,pinode **item)
{
#if PINT_ENABLE_PCACHE
    int i = 0;

    /* Grab a mutex */
    gen_mutex_lock(pvfs_pcache.mt_lock);

    /* No match found */
    *item = NULL;

    /* Search the cache */
    for(i = pvfs_pcache.top; i != BAD_LINK; i = pvfs_pcache.element[i].next)
    {
	if ((refn.handle == pvfs_pcache.element[i].pnode->pinode_ref.handle)
	    && (refn.fs_id == pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
	{
	    *item = pvfs_pcache.element[i].pnode;
	    PINT_pcache_remove_element(i);
	}
    }

    /* Release the mutex */
    gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif
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

    /*TODO: we're sending the pointer back to the user (saved by the caller)
     * already, but should we deallocate in the lru case also?
     */
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

    /* depending on where the dentry is in the list, we have to do different
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
 * implements cache replacement policy(LRU) if needed 
 *
 * returns 0 on success, -errno on failure
 */
static int PINT_pcache_get_lru(void)
{
    int new = 0, i = 0;

    if (pvfs_pcache.count == PINT_PCACHE_MAX_ENTRIES)
    {
	new = pvfs_pcache.bottom;
	pvfs_pcache.bottom = pvfs_pcache.element[new].prev;
	pvfs_pcache.element[pvfs_pcache.bottom].next = BAD_LINK;

	/*TODO: should we deallocate here? add to a linked list to dealloc later..? */

	return new;
    }
    else
    {
	for(i = 0; i < PINT_PCACHE_MAX_ENTRIES; i++)
	{
	    if (pvfs_pcache.element[i].status == STATUS_UNUSED)
	    {
		pvfs_pcache.count++;
		return i;
	    }
	}
    }

    gossip_ldebug(PCACHE_DEBUG, "error getting least recently used dentry.\n");
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

    /* Adding the element to the cache */
    pvfs_pcache.element[new].status = STATUS_USED;
    pvfs_pcache.element[new].pnode = pnode;	
    pvfs_pcache.element[new].prev = BAD_LINK;
    pvfs_pcache.element[new].next = pvfs_pcache.top;
    /* Make previous element point to new entry */
    if (pvfs_pcache.top != BAD_LINK)
	pvfs_pcache.element[pvfs_pcache.top].prev = new;
    /* Readjust the top */
    pvfs_pcache.top = new;

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

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

