/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */

#include <pcache.h>

static int pcache_get_next_free(pcache *cache);
static int pcache_add_pinode(pcache *cache,pinode *pnode);
static void pcache_merge_pinode(pinode *p1,pinode *p2);
static void pcache_pinode_copy(pinode *new, pinode *old);

pcache pvfs_pcache;

/* pcache_initialize
 *
 * initializes the cache
 *
 * returns 0 on success, -1 on failure
 */
int pcache_initialize(pcache *cache)
{
	int16_t i = 0;
	/* Init the mutex lock */
	cache->mt_lock = gen_mutex_build();
	cache->top = -1;
	cache->bottom = -1;
	cache->free = 0;
	/* Form a link between cache elements */
	for(i = 0; i < MAX_ENTRIES; i++)
	{
		cache->element[i].prev = i - 1;
		if ((i + 1) == MAX_ENTRIES)
			cache->element[i].next = -1;
		else
			cache->element[i].next = i + 1;
	}
	return(0);
}

/* pcache_finalize
 *
 * deallocates memory as needed
 *
 * returns 0 on success, -1 on failure
 */
int pcache_finalize(pcache cache)
{
	int16_t i = 0;

	/* Grab the mutex - do we need to do this? */
	gen_mutex_lock(cache.mt_lock);

	/* Deallocate the pinodes */
	for(i = 0; i < MAX_ENTRIES; i++)
	{
		pcache_pinode_dealloc(cache.element[i].pnode);
	}
	/* Release the mutex */
	gen_mutex_unlock(cache.mt_lock);
	
	/* Destroy the mutex */
	gen_mutex_destroy(cache.mt_lock);
	
	return(0);
}

/* pcache_insert
 *
 * adds/merges an element to the pinode cache
 *
 * returns 0 on success, -1 on error
 */
int pcache_insert(pcache *cache,pinode *pnode )
{
	int16_t i = 0,entry = 0;
	int ret = 0;
	unsigned char entry_found = 0;
	
	/* Grab the mutex */
	gen_mutex_lock(cache->mt_lock);
	
	/* Search the cache */
	for(i = cache->top;i != -1;)
	{
		if ((pnode->pinode_ref.handle ==\
			cache->element[i].pnode->pinode_ref.handle)\
			&& (pnode->pinode_ref.fs_id ==\
				cache->element[i].pnode->pinode_ref.fs_id))
		{
			entry_found = 1;
			entry = i;
			break;
		}
		/* Get next element in cache */
		i = cache->element[i].next;
	}

	/* Add/Merge element to the cache */
	if (!entry_found)
	{
		/* Element absent in cache, add it */
		ret = pcache_add_pinode(cache,pnode);
	}
	else
	{
		/* Element present in the cache, merge it */
		/* Pinode with same <handle,fs_id> found...
		 * Now, we need to merge the pinode values with
		 * the older one 
		 */
		pcache_merge_pinode(cache->element[entry].pnode,pnode);
	}

	/* Release the mutex */
	gen_mutex_unlock(cache->mt_lock);
	
	return(0);
}

/* pcache_lookup
 *
 * find the element in the cache
 *
 * returns 0 on success 
 */
int pcache_lookup(pcache *cache,pinode_reference refn,pinode *pinode_ptr)
{
	int i = 0;
	
	/* Check for allocated address */
	if (!pinode_ptr)
		return(-ENOMEM);

	/* Grab a mutex */
	gen_mutex_lock(cache->mt_lock);

	/* No match found */
	//*item = -1;
	pinode_ptr->pinode_ref.handle = -1;
	
	/* Search the cache */
	for(i = cache->top; i != -1;)
	{
		if ((refn.handle == cache->element[i].pnode->pinode_ref.handle)\
			&& (refn.fs_id == cache->element[i].pnode->pinode_ref.fs_id))
		{
			//*item = i;
			pcache_pinode_copy(pinode_ptr,cache->element[i].pnode);
			break;
		}
		/* Get next element in cache */
		i = cache->element[i].next;
	}

	/* Release the mutex */
	gen_mutex_unlock(cache->mt_lock);

	return(0);
}

/* pcache_remove
 *
 * remove an element from the pinode cache
 *
 * returns 0 on success, -1 on failure
 */
int pcache_remove(pcache *cache, pinode_reference refn,pinode **item)
{
	int16_t i = 0,prev = 0,next = 0;
	
	/* Grab a mutex */
	gen_mutex_lock(cache->mt_lock);

	/* No match found */
	*item = NULL;

	/* Search the cache */
	for(i = cache->top; i != -1;)
	{
		if ((refn.handle == cache->element[i].pnode->pinode_ref.handle)\
			&& (refn.fs_id == cache->element[i].pnode->pinode_ref.fs_id))
		{
			/* Is it the first item? */
			if (i == cache->top)
			{
				/* Adjust top */
				cache->top = cache->element[i].next;
				cache->element[cache->top].prev = -1;
			}
			/* Is it the last item? */
			else if(i == cache->bottom)
			{
				/* Adjust bottom */
				cache->bottom = cache->element[i].prev;
				cache->element[cache->bottom].next = -1;
			}
			else
			{
				/* Item in the middle */
				prev = cache->element[i].prev;
				next = cache->element[i].next;
				cache->element[prev].next = next;
				cache->element[next].prev = prev;
			}
			*item = cache->element[i].pnode;
			/* Adjust free list */
			cache->element[i].next = cache->free;
			cache->element[i].prev = -1;
			cache->free = i;
			//*item = i;
			break;
		}
		/* Get next element in cache */
		i = cache->element[i].next;
	}

	/* Release the mutex */
	gen_mutex_unlock(cache->mt_lock);

	return(0);

}

/* pcache_get_next_free
 *
 * implements cache replacement policy(LRU) if needed 
 *
 * returns 0 on success, -errno on failure
 */
static int pcache_get_next_free(pcache *cache)
{
	int16_t free = 0;

	/* Check if free element exists? */
	if (cache->free == -1)
	{
		free = cache->bottom;
		/* Replace at bottom of cache */
		/* Assumption is that LRU element is at bottom */
		cache->bottom = cache->element[free].prev;
		cache->element[cache->bottom].next = -1;
		/* Dealloc the pinode */
		pcache_pinode_dealloc(cache->element[free].pnode);
		cache->free = free;
	}

	return(0);
}

/* pcache_merge_pinode
 *
 * merges pinodes based on timestamps
 *
 * returns nothing
 */
static void pcache_merge_pinode(pinode *p1,pinode *p2)
{
	/* Check the attribute timestamps to see which
	 * pinode is the latest 
	 */
	if (p1->tstamp_attr.tv_sec > p2->tstamp_attr.tv_sec ||\
			(p1->tstamp_attr.tv_sec == p2->tstamp_attr.tv_sec &&\
			 p1->tstamp_attr.tv_usec > p2->tstamp_attr.tv_usec))
	{
		p2->attr = p1->attr;
	}

	/* Check the size timestamps to see which has the latest size */
	if (p1->tstamp_size.tv_sec > p2->tstamp_size.tv_usec ||\
			(p1->tstamp_size.tv_sec == p2->tstamp_size.tv_sec &&\
			 p1->tstamp_size.tv_usec > p2->tstamp_size.tv_usec))
	{	
		p2->size = p1->size;
	}
}

/* pcache_add_pinode
 *
 * adds pinode to cache and updates cache variables
 *
 * returns 0 on success
 */
static int pcache_add_pinode(pcache *cache,pinode *pnode)
{
	int16_t free = 0;

	/* Get the free item */
	pcache_get_next_free(cache);

	/* Update the free list by pointing to next free item */
	free = cache->free;
	cache->free = cache->element[free].next;
	if (cache->free >= 0)
		cache->element[cache->free].prev = -1;
	
	/* Adding the element to the cache */
	cache->element[free].pnode = pnode;	
	cache->element[free].prev = -1;
	cache->element[free].next = cache->top;
	/* Make previous element point to new entry */
	cache->element[cache->top].prev = free;
	/* Readjust the top */
	cache->top = free;

	return(0);
}

/* pcache_pinode_alloc 
 *
 * allocate a pinode 
 *
 * returns 0 on success, -1 on failure
 */
int pcache_pinode_alloc(pinode **pnode)
{
	*pnode = (pinode *)malloc(sizeof(pinode));
	if (!(*pnode))
		return(-ENOMEM);

	memset(*pnode,0,sizeof(pinode));	
	return(0);
}

/* pcache_pinode_dealloc 
 *
 * deallocate a pinode 
 *
 * returns nothing
 */
void pcache_pinode_dealloc(pinode *pnode)
{
	if (pnode)
		free(pnode);
}

/* pinode_copy 
 *
 * copy a pinode to another 
 *
 * returns nothing
 */
static void pcache_pinode_copy(pinode *new, pinode *old)
{
	new->pinode_ref = old->pinode_ref; 
	new->attr = old->attr;
	new->mask = old->mask;
	new->size = old->size;
	new->tstamp_handle = old->tstamp_handle;
	new->tstamp_attr = old->tstamp_attr;
	new->tstamp_size = old->tstamp_size;
}
