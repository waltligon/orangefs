/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* PVFS directory cache implementation */

#include <pint-dcache.h>

/* Timeout for a dcache entry */
static struct timeval dentry_to;

/* TODO: Figure out how timeouts are going to be used to invalidate
 * the cache entries 
 */

static void dcache_remove_dentry(struct dcache *cache, int16_t item);
static int dcache_update_dentry_timestamp(dcache_entry entry); 
static int check_dentry_expiry(struct timeval t2);
static int dcache_add_dentry(struct dcache *cache,char *name,
		pinode_reference parent,pinode_reference entry);
static int dcache_get_next_free(struct dcache *cache);
static int compare(struct dcache_t element,char *name,pinode_reference refn);

/* The PVFS Dcache */
dcache pvfs_dcache;

/* dcache_lookup
 *
 * search PVFS directory cache for specific entry
 *
 * returns 0 on success, -1 on failure
 */
int dcache_lookup(struct dcache *cache,char *name,pinode_reference parent,
		pinode_reference *entry)
{
	int16_t i = 0;
	int ret = 0;

	if (!name)
		return(-ENOMEM);

	/* Grab a mutex */
	gen_mutex_lock(cache->mt_lock);

	/* No match found */
	entry->handle = PINT_DCACHE_HANDLE_INVALID;	
	
	/* Search the cache */
	for(i = cache->top; i != -1;)
	{
		if (compare(cache->element[i],name,parent))
		{
			/* Got a match */
			/* Check the timestamp for validity */
			ret = check_dentry_expiry(cache->element[i].dentry.tstamp_valid);
			if (ret < 0)
			{
				/* Dentry is stale */
				/* Remove the entry from the cache */
				dcache_remove_dentry(cache,i);
				/* Release the mutex */
				gen_mutex_unlock(cache->mt_lock);
				
				return(0);
			}
			entry->handle = cache->element[i].dentry.entry.handle;
			entry->fs_id = cache->element[i].dentry.entry.fs_id;	
			break;
		}
		/* Get next element in cache */
		i = cache->element[i].next;
	}

	/* Release the mutex */
	gen_mutex_unlock(cache->mt_lock);

	return(0);
}

/* dcache_insert
 *
 * insert an entry into PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int dcache_insert(struct dcache *cache,char *name,pinode_reference entry,
		pinode_reference parent)
{
	int16_t i = 0,index = 0, ret = 0;
	unsigned char entry_found = 0;
	
	/* Grab a mutex */
	gen_mutex_lock(cache->mt_lock);

	/* Search the cache */
	for(i = cache->top; i != -1;)
	{
		if (compare(cache->element[i],name,parent))
		{
			entry_found = 1;
			index = i;
			break;
		}
		/* Get next element in cache */
		i = cache->element[i].next;
		/*if (i == -1)
			break;*/
	}
	
	/* Add/Merge element to the cache */
	if (!entry_found)
	{
		/* Element absent in cache, add it */
		dcache_add_dentry(cache,name,parent,entry);
	}
	else
	{
		/* For now we just leave the entry in place, update its
		 * timestamp and return 
		 */
		ret = dcache_update_dentry_timestamp(cache->element[index].dentry); 
		if (ret < 0)
		{
			/* Release the mutex */
			gen_mutex_unlock(cache->mt_lock);
			return(ret);
		}
	}

	/* Release the mutex */
	gen_mutex_unlock(cache->mt_lock);
	
	return(0);
}

/* dcache_remove
 *
 * remove a particular entry from the PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int dcache_remove(struct dcache *cache,char *name,pinode_reference parent,
		unsigned char *item_found)
{
	int16_t i = 0;

	if (!name)
		return(-ENOMEM);

	/* Grab the mutex */
	gen_mutex_lock(cache->mt_lock);

	/* No match found */
	*item_found = 0;
	
	/* Search the cache */
	for(i = cache->top; i != -1;)
	{
		if (compare(cache->element[i],name,parent))
		{
			/* Remove the cache element */
			dcache_remove_dentry(cache,i);
			*item_found = 1;
			break;
		}
		/* Get next element in cache */
		i = cache->element[i].next;
	}

	/* Relase the mutex */
	gen_mutex_unlock(cache->mt_lock);

	return(0);
}

/* dcache_flush
 * 
 * remove all entries from the PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int dcache_flush(struct dcache cache)
{
	return(-ENOSYS);
}

/* pint_dinitialize
 *
 * initiliaze the PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int dcache_initialize(struct dcache *cache)
{
	int16_t i = 0;	

	/* Init the mutex lock */
	cache->mt_lock = gen_mutex_build();
	cache->top = -1;
	cache->bottom = -1;
	cache->free = 0;
	/* Form the link between the cache elements */
	for(i = 0;i < PINT_DCACHE_MAX_ENTRIES; i++)
	{
		cache->element[i].prev = i - 1;
		if ((i + 1) == PINT_DCACHE_MAX_ENTRIES)
			cache->element[i].next = -1;
		else
			cache->element[i].next = i + 1;
	}
	return(0);
}

/* pint_dfinalize
 *
 * close down the PVFS directory cache framework
 *
 * returns 0 on success, -1 on failure
 */
int dcache_finalize(struct dcache *cache)
{
	/* make sure this doesn't look like a populated dcache if someone
	 * accesses it late 
	 */
	cache->top = -1;
	cache->bottom = -1;
	cache->free = 0;

	/* Destroy the mutex */
	gen_mutex_destroy(cache->mt_lock);

	return(0);
}

/* compare
 *
 *	compares a dcache entry to the search key
 *
 * returns 0 on success, -errno on failure
 */
static int compare(struct dcache_t element,char *name,pinode_reference refn)
{

	/* Does the cache entry match the search key? */
	if (!strncmp(name,element.dentry.name,strlen(name)) &&
		element.dentry.parent.handle == refn.handle
		&& element.dentry.parent.fs_id == refn.fs_id) 
	{
		return(1);
	}
	
	return(0);
}

/* dcache_add_dentry
 *
 * add a dentry to the dcache
 *
 * returns 0 on success, -errno on failure
 */
static int dcache_add_dentry(struct dcache *cache,char *name,
		pinode_reference parent,pinode_reference entry)
{
	int16_t free = 0;
	int size = strlen(name),ret = 0;

	/* Get the free item */
	dcache_get_next_free(cache);

	/* Update the free list by pointing to next free item */
	free = cache->free;
	cache->free = cache->element[free].next;
	if (cache->free >= 0)
		cache->element[cache->free].prev = -1;
		
	/* Adding the element to the cache */
	cache->element[free].dentry.parent = parent;
	cache->element[free].dentry.entry = entry;
	strncpy(cache->element[free].dentry.name,name,size);
	cache->element[free].dentry.name[size] = '\0';
	/* Set the timestamp */
	ret = gettimeofday(&cache->element[free].dentry.tstamp_valid,NULL);
	if (ret < 0)
	{
		return(ret);	
	}
	cache->element[free].prev = -1;
	cache->element[free].next = cache->top;
	/* Make previous element point to new entry */
	cache->element[cache->top].prev = free;
	/* Readjust the top */
	cache->top = free;

	return(0);
}

/* dcache_get_next_free
 *
 * implements cache replacement policy(LRU) if needed
 *
 * returns 0 on success, -errno on failure
 */
static int dcache_get_next_free(struct dcache *cache)
{
	int16_t free = 0;

	/* Check if free element exists */
	if (cache->free == -1)
	{
		free = cache->bottom;
		/* Replace at bottom of cache */
		/* Assumption is that LRU element is at bottom */
		cache->bottom = cache->element[free].prev;
		cache->element[cache->bottom].next = -1;
		memset(&cache->element[free],0,sizeof(struct dcache_t));
		cache->free = free;
	}

	return(0);
}

/* check_dentry_expiry
 *
 * need to validate the dentry against the timestamp
 *
 * returns 0 on success, -1 on failure
 */
static int check_dentry_expiry(struct timeval t2)
{
	int ret = 0;
	struct timeval cur_time;

	ret = gettimeofday(&cur_time,NULL);
	if (ret < 0)
	{
		return(ret);
	}
	/* Does the timestamp exceed the current time? 
	 * If yes, dentry is valid. If no, it is stale.
	 */
	if (t2.tv_sec > cur_time.tv_sec || (t2.tv_sec == cur_time.tv_sec &&
			t2.tv_usec > cur_time.tv_usec))
		return(0);
	
	/* Dentry is stale */
	return(-1);
}

/* dcache_remove_dentry
 *
 * Handles the actual manipulation of the cache to handle removal
 *
 * returns nothing
 */
static void dcache_remove_dentry(struct dcache *cache, int16_t item)
{
	int16_t i = item,prev = 0,next = 0;

	/* Handle the details of removing an element from the cache */

	/* Is it the first item? */
	if (i == cache->top)
	{
		/* Adjust top */
		cache->top = cache->element[i].next;
		cache->element[cache->top].prev = -1;
	}
	/* Is it the last item? */
	else if (i == cache->bottom)
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
	/* Adjust the free list */
	cache->element[i].next = cache->free;
	cache->element[i].prev = -1;
	cache->free = i;
}

/* dcache_update_dentry_timestamp
 *
 * updates the timestamp of the dcache entry
 *
 * returns 0 on success, -1 on failure
 */
static int dcache_update_dentry_timestamp(dcache_entry entry) 
{
	int ret = 0;
	struct timeval cur_time;
	int64_t val = 0;
	
	/* Update the timestamp */
	ret = gettimeofday(&cur_time,NULL);
	if (ret < 0)
	{
		return(-1);
	}
	/* Handle the case where sum of usecs adds an extra second */
	val = cur_time.tv_usec + dentry_to.tv_usec;
	if (val >= 1000000)
	{
		entry.tstamp_valid.tv_usec = val % 1000000; 
		entry.tstamp_valid.tv_sec = cur_time.tv_sec + dentry_to.tv_sec + 1;
	}
	else
	{
		entry.tstamp_valid.tv_usec = val; 
		entry.tstamp_valid.tv_sec = cur_time.tv_sec + dentry_to.tv_sec;
	}
	return(0);
}
