/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#include <pcache.h>

static int PINT_pcache_get_next_free(void);
static int PINT_pcache_add_pinode(pinode *pnode);
static void PINT_pcache_merge_pinode(pinode *p1,pinode *p2);
static int check_expiry(pinode *pnode);

static pcache pvfs_pcache;

/* PINT_pcache_initialize
 *
 * initializes the cache
 *
 * returns 0 on success, -1 on failure
 */
int PINT_pcache_initialize(void)
{
	int16_t i = 0;
#if 0
	/* TODO: does a mutex need to go here?*/
	/* Init the mutex lock */
	pvfs_pcache.mt_lock = gen_mutex_build();
#endif
	pvfs_pcache.top = -1;
	pvfs_pcache.bottom = -1;
	pvfs_pcache.free = 0;
	/* Form a link between cache elements */
	for(i = 0; i < MAX_ENTRIES; i++)
	{
		pvfs_pcache.element[i].prev = i - 1;
		pvfs_pcache.element[i].pnode = NULL;
		if ((i + 1) == MAX_ENTRIES)
			pvfs_pcache.element[i].next = -1;
		else
			pvfs_pcache.element[i].next = i + 1;
	}
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
	int16_t i = 0;

#if 0
	/* TODO: does a mutex need to go here?*/
	/* Grab the mutex - do we need to do this? */
	gen_mutex_lock(pvfs_pcache.mt_lock);
#endif

	/* Deallocate the pinodes */
	for(i = 0; i < MAX_ENTRIES; i++)
	{
		PINT_pcache_pinode_dealloc(pvfs_pcache.element[i].pnode);
	}
#if 0
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
	int16_t i = 0,entry = 0;
	int ret = 0;
	unsigned char entry_found = 0;
	
#if 0
	/* TODO: does a mutex need to go here?*/
	/* Grab the mutex */
	gen_mutex_lock(pvfs_pcache.mt_lock);
#endif
	
	/* Search the cache */
	for(i = pvfs_pcache.top; i != -1; i = pvfs_pcache.element[i].next)
	{
		if ((pnode->pinode_ref.handle ==\
			pvfs_pcache.element[i].pnode->pinode_ref.handle)\
			&& (pnode->pinode_ref.fs_id ==\
				pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
		{
			entry_found = 1;
			entry = i;
			break;
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
	}

#if 0
	/* TODO: does a mutex need to go here?*/
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
	int i = 0, ret;

#if 0
	/* TODO: does a mutex need to go here?*/
	/* Grab a mutex */
	gen_mutex_lock(pvfs_pcache.mt_lock);
#endif

	/* No match found */
        ret = PCACHE_LOOKUP_FAILURE;
	
	/* Search the cache */
	for(i = pvfs_pcache.top; i != -1; i = pvfs_pcache.element[i].next)
	{
		if ((refn.handle == pvfs_pcache.element[i].pnode->pinode_ref.handle)
			&& (refn.fs_id == pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
		{
			/* we don't want old pinodes */
			if (check_expiry(pvfs_pcache.element[i].pnode) == PINODE_VALID)
			{
				(*pinode_ptr) = pvfs_pcache.element[i].pnode;
				ret = PCACHE_LOOKUP_SUCCESS;
			}
		}
	}

#if 0
	/* TODO: does a mutex need to go here?*/
	/* Release the mutex */
	gen_mutex_unlock(pvfs_pcache.mt_lock);
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
	int16_t i = 0,prev = 0,next = 0;
	
#if 0
	/* TODO: does a mutex need to go here?*/
	/* Grab a mutex */
	gen_mutex_lock(pvfs_pcache.mt_lock);
#endif

	/* No match found */
	*item = NULL;

	/* Search the cache */
	for(i = pvfs_pcache.top; i != -1;)
	{
		if ((refn.handle == pvfs_pcache.element[i].pnode->pinode_ref.handle)\
			&& (refn.fs_id == pvfs_pcache.element[i].pnode->pinode_ref.fs_id))
		{
			/* Is it the first item? */
			if (i == pvfs_pcache.top)
			{
				/* Adjust top */
				pvfs_pcache.top = pvfs_pcache.element[i].next;
				pvfs_pcache.element[pvfs_pcache.top].prev = -1;
			}
			/* Is it the last item? */
			else if(i == pvfs_pcache.bottom)
			{
				/* Adjust bottom */
				pvfs_pcache.bottom = pvfs_pcache.element[i].prev;
				pvfs_pcache.element[pvfs_pcache.bottom].next = -1;
			}
			else
			{
				/* Item in the middle */
				prev = pvfs_pcache.element[i].prev;
				next = pvfs_pcache.element[i].next;
				pvfs_pcache.element[prev].next = next;
				pvfs_pcache.element[next].prev = prev;
			}
			*item = pvfs_pcache.element[i].pnode;
			/* Adjust free list */
			pvfs_pcache.element[i].next = pvfs_pcache.free;
			pvfs_pcache.element[i].prev = -1;
			pvfs_pcache.free = i;
			//*item = i;
			break;
		}
		/* Get next element in cache */
		i = pvfs_pcache.element[i].next;
	}

#if 0
	/* TODO: does a mutex need to go here?*/
	/* Release the mutex */
	gen_mutex_unlock(pvfs_pcache.mt_lock);
#endif

	return(0);

}

/* PINT_pcache_get_next_free
 *
 * implements cache replacement policy(LRU) if needed 
 *
 * returns 0 on success, -errno on failure
 */
static int PINT_pcache_get_next_free(void)
{
	int16_t free = 0;

	/* Check if free element exists? */
	if (pvfs_pcache.free == -1)
	{
		free = pvfs_pcache.bottom;
		/* Replace at bottom of cache */
		/* Assumption is that LRU element is at bottom */
		pvfs_pcache.bottom = pvfs_pcache.element[free].prev;
		pvfs_pcache.element[pvfs_pcache.bottom].next = -1;
		/* Dealloc the pinode */
		PINT_pcache_pinode_dealloc(pvfs_pcache.element[free].pnode);
		pvfs_pcache.free = free;
	}

	return(0);
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
	int16_t free = 0;

	/* Get the free item */
	PINT_pcache_get_next_free();

	/* Update the free list by pointing to next free item */
	free = pvfs_pcache.free;
	pvfs_pcache.free = pvfs_pcache.element[free].next;
	if (pvfs_pcache.free >= 0)
		pvfs_pcache.element[pvfs_pcache.free].prev = -1;
	
	/* Adding the element to the cache */
	pvfs_pcache.element[free].pnode = pnode;	
	pvfs_pcache.element[free].prev = -1;
	pvfs_pcache.element[free].next = pvfs_pcache.top;
	/* Make previous element point to new entry */
	pvfs_pcache.element[pvfs_pcache.top].prev = free;
	/* Readjust the top */
	pvfs_pcache.top = free;

	return(0);
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
	if (pnode->attr.objype == ATTR_META)
	{
		if(pnode->attr.u.meta.nr_datafiles > 0)
			free(pnode->attr.u.meta->dfh);
	}
	if (pnode != NULL)
		free(pnode);
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
        if (pnode->tstamp.tv_sec > cur.tv_sec || (pnode->tstamp.tv_sec == cur.tv_sec &&
                                pnode->tstamp.tv_usec > cur.tv_usec))
                        return PINODE_VALID;

        return PINODE_EXPIRED;
}


#if 0
/* pinode_copy 
 *
 * copy a pinode to another 
 *
 * returns nothing
 */
static void PINT_pcache_pinode_copy(pinode *new, pinode *old)
{
	new->pinode_ref = old->pinode_ref; 
	new->attr = old->attr;
	new->mask = old->mask;
	new->size = old->size;
	new->tstamp = old->tstamp;
	new->size_flag = old->size_flag;
}
#endif
