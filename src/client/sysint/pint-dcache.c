/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* PVFS directory cache implementation */

#include <pint-dcache.h>
#include <assert.h>

/* Dcache Entry */
struct dcache_entry_s {
	PVFS_pinode_reference parent;   /* the pinode of the parent directory */
	char name[PVFS_SEGMENT_MAX];  /* PVFS object name */
	PVFS_pinode_reference entry;    /* the pinode of entry in parent */
	struct timeval tstamp_valid;  /* timestamp indicating validity period */
};
typedef struct dcache_entry_s dcache_entry;

/* Dcache element */
struct dcache_t {
	dcache_entry dentry;
	int prev;
	int next;
	int status;	    /*whether the entry is in use or not*/
};

/* Cache Management structure */
struct dcache_s {
	struct dcache_t element[PINT_DCACHE_MAX_ENTRIES];
	int count;
	int top;
	int bottom;
	gen_mutex_t *mt_lock;
};
typedef struct dcache_s dcache;

#define BAD_LINK -1
#define STATUS_UNUSED 0
#define STATUS_USED 1

/* change this to 0 to disable directory caching
 * all calls return success, but lookups will not succeed, inserts/removes won't
 * actually change anything if the cache is disabled
 */
#define ENABLE_DCACHE 1

static void dcache_remove_dentry(int item);
static void dcache_rotate_dentry(int item);
static int dcache_update_dentry_timestamp(dcache_entry* entry); 
static int check_dentry_expiry(struct timeval t2);
static int dcache_add_dentry(char *name,
	PVFS_pinode_reference parent, PVFS_pinode_reference entry);
static int dcache_get_lru(void);
static int compare(struct dcache_t element,char *name, PVFS_pinode_reference refn);

/* The PVFS Dcache */
static dcache* cache = NULL;

/* dcache_lookup
 *
 * search PVFS directory cache for specific entry
 *
 * returns 0 on success, -1 on failure
 */
int PINT_dcache_lookup(
    char *name,
    PVFS_pinode_reference parent,
    PVFS_pinode_reference *entry)
{
#if ENABLE_DCACHE
    int i = 0;
    int ret = 0;

    if (!name)
	return(-ENOMEM);

    /* Grab a mutex */
    gen_mutex_lock(cache->mt_lock);

    /* No match found */
    entry->handle = PINT_DCACHE_HANDLE_INVALID;	

    /* Search the cache */
    for(i = cache->top; i != BAD_LINK; i = cache->element[i].next)
    {
	if (compare(cache->element[i],name,parent))
	{
	    gossip_ldebug(DCACHE_DEBUG, "dcache match; checking timestamp.\n");
	    ret = check_dentry_expiry(cache->element[i].dentry.tstamp_valid);
	    if (ret < 0)
	    {
		gossip_ldebug(DCACHE_DEBUG, "dcache entry expired.\n");
		/* Dentry is stale */
		/* Remove the entry from the cache */
		dcache_remove_dentry(i);
		/* Release the mutex */
		gen_mutex_unlock(cache->mt_lock);

		return(0);
	    }

	    /*update links so that this dentry is at the top of our list*/
	    dcache_rotate_dentry(i);

	    /* TODO: should we extend the timeout period here? */
	    entry->handle = cache->element[i].dentry.entry.handle;
	    entry->fs_id = cache->element[i].dentry.entry.fs_id;	
	    gossip_ldebug(DCACHE_DEBUG, "dcache entry valid.\n");
	    break;
	    }
    }

    /* Release the mutex */
    gen_mutex_unlock(cache->mt_lock);

    return(0);
#else
    entry->handle = PINT_DCACHE_HANDLE_INVALID;
    return(0);
#endif
}

/* dcache_rotate_dentry()
 *
 * moves the specified item to the top of the dcache linked list to prevent it
 * from being identified as the least recently used item in the cache.
 *
 * no return value
 */
static void dcache_rotate_dentry(int item)
{
    int prev = 0, next = 0, new_bottom;
    if (cache->top != cache->bottom) 
    {
	if(cache->top != item)
	{
	    /*only move links if there's more than one thing in the list*/

	    if (cache->bottom == item)
	    {
		new_bottom = cache->element[cache->bottom].prev;

		cache->element[new_bottom].next = BAD_LINK;
		cache->bottom = new_bottom;
	    }
	    else
	    {
		/*somewhere in the middle*/
		next = cache->element[item].next;
		prev = cache->element[item].prev;

		cache->element[prev].next = next;
		cache->element[next].prev = prev;
	    }

	    cache->element[cache->top].prev = item;

	    cache->element[item].next = cache->top;
	    cache->element[item].prev = BAD_LINK;
	    cache->top = item;
	}
    }
}

/* dcache_insert
 *
 * insert an entry into PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int PINT_dcache_insert(
	char *name,
	PVFS_pinode_reference entry,
	PVFS_pinode_reference parent)
{
#if ENABLE_DCACHE
	int i = 0, index = 0, ret = 0;
	unsigned char entry_found = 0;
	
	/* Grab a mutex */
	gen_mutex_lock(cache->mt_lock);

	/* Search the cache */
	for (i = cache->top; i != BAD_LINK; i = cache->element[i].next)
	{
		if (compare(cache->element[i],name,parent))
		{
			entry_found = 1;
			index = i;
			break;
		}
	}
	
	/* Add/Merge element to the cache */
	if (entry_found == 0)
	{
		/* Element not in cache, add it */
		dcache_add_dentry(name,parent,entry);
	}
	else
	{
		/* We move the dentry to the top of the list, update its
		 * timestamp and return 
		 */
		gossip_ldebug(DCACHE_DEBUG, "dache inserting entry already present; timestamp update.\n");
		dcache_rotate_dentry(index);
		ret = dcache_update_dentry_timestamp(
			&cache->element[index].dentry); 
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
#else
    return(0);
#endif
}

/* dcache_remove
 *
 * remove a particular entry from the PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int PINT_dcache_remove(
	char *name,
	PVFS_pinode_reference parent,
	int *item_found)
{
#if ENABLE_DCACHE
	int i = 0;

	if (name == NULL)
		return(-EINVAL);

	/* Grab the mutex */
	gen_mutex_lock(cache->mt_lock);

	/* No match found */
	*item_found = 0;
	
	/* Search the cache */
	for(i = cache->top; i != BAD_LINK; i = cache->element[i].next)
	{
		if (compare(cache->element[i],name,parent))
		{
			/* Remove the cache element */
			dcache_remove_dentry(i);
			*item_found = 1;
			gossip_ldebug(DCACHE_DEBUG, "dcache removing entry (%lld).\n", parent.handle);
			break;
		}
	}

	if(*item_found == 0)
	{
		gossip_ldebug(DCACHE_DEBUG, "dcache found no entry to remove.\n");
	}

	/* Relase the mutex */
	gen_mutex_unlock(cache->mt_lock);

	return(0);
#else
    return(0);
#endif
}

/* dcache_flush
 * 
 * remove all entries from the PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int PINT_dcache_flush(void)
{
	return(-ENOSYS);
}

/* pint_dinitialize
 *
 * initiliaze the PVFS directory cache
 *
 * returns 0 on success, -1 on failure
 */
int PINT_dcache_initialize(void)
{
#if ENABLE_DCACHE
    int i = 0;	

    cache = (dcache*)malloc(sizeof(dcache));
    if(cache == NULL)
    {
	return(-ENOMEM);
    }

    /* Init the mutex lock */
    cache->mt_lock = gen_mutex_build();
    cache->top = BAD_LINK;
    cache->bottom = 0;
    cache->count = 0;

    for(i = 0;i < PINT_DCACHE_MAX_ENTRIES; i++)
    {
	cache->element[i].prev = BAD_LINK;
	cache->element[i].next = BAD_LINK;
	cache->element[i].status = STATUS_UNUSED;
    }
    return(0);
#else
    return(0);
#endif
}

/* pint_dfinalize
 *
 * close down the PVFS directory cache framework
 *
 * returns 0 on success, -1 on failure
 */
int PINT_dcache_finalize(void)
{
#if ENABLE_DCACHE

    if (cache != NULL)
    {
	/* Destroy the mutex */
	gen_mutex_destroy(cache->mt_lock);
	free(cache);
    }

    cache = NULL;

    return(0);
#else
    return(0);
#endif
}

/* compare
 *
 *	compares a dcache entry to the search key
 *
 * returns 0 on success, -errno on failure
 */
static int compare(struct dcache_t element,char *name,PVFS_pinode_reference refn)
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
static int dcache_add_dentry(char *name, PVFS_pinode_reference parent,
		PVFS_pinode_reference entry)
{
	int new = 0, ret = 0;
	int size = strlen(name) + 1; /* size includes null terminator*/

	/* Get the free item */
	new = dcache_get_lru();

	/* Add the element to the cache */
	cache->element[new].status = STATUS_USED;
	cache->element[new].dentry.parent = parent;
	cache->element[new].dentry.entry = entry;
	memcpy(cache->element[new].dentry.name,name,size);
	/* Set the timestamp */
	ret = dcache_update_dentry_timestamp(
		&cache->element[new].dentry);
	if (ret < 0)
	{
		return(ret);	
	}
	cache->element[new].prev = BAD_LINK;
	cache->element[new].next = cache->top;
	/* Make previous element point to new entry */
	if (cache->top != BAD_LINK)
		cache->element[cache->top].prev = new;

	cache->top = new;

	return(0);
}

/* dcache_get_lru
 *
 * this function gets the least recently used cache entry (assuming a full 
 * cache) or searches through the cache for the first unused slot (if there
 * are some free slots)
 *
 * returns 0 on success, -errno on failure
 */
static int dcache_get_lru(void)
{
    int new = 0, i = 0;

    if (cache->count == PINT_DCACHE_MAX_ENTRIES)
    {
	new = cache->bottom;
	cache->bottom = cache->element[new].prev;
	cache->element[cache->bottom].next = BAD_LINK;
	return new;
    }
    else
    {
	for(i = 0; i < PINT_DCACHE_MAX_ENTRIES; i++)
	{
	    if (cache->element[i].status == STATUS_UNUSED)
	    {
		cache->count++;
		return i;
	    }
	}
    }

    gossip_ldebug(DCACHE_DEBUG, "error getting least recently used dentry.\n");
    gossip_ldebug(DCACHE_DEBUG, "cache->count = %d max_entries = %d.\n", cache->count, PINT_DCACHE_MAX_ENTRIES);
    assert(0);
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
    {
	return(0);
    }
	
    /* Dentry is stale */
    return(-1);
}

/* dcache_remove_dentry
 *
 * Handles the actual manipulation of the cache to handle removal
 *
 * returns nothing
 */
static void dcache_remove_dentry(int item)
{
    int prev = 0,next = 0;

    cache->element[item].status = STATUS_UNUSED;
    memset(&cache->element[item].dentry.name, 0, PVFS_SEGMENT_MAX );
    cache->count--;

    /* if there's exactly one item in the list, just get rid of it*/
    if (cache->top == cache->bottom)
    {
	cache->top = 0;
	cache->bottom = 0;
	cache->element[item].prev = BAD_LINK;
	cache->element[item].next = BAD_LINK;
	return;
    }

    /* depending on where the dentry is in the list, we have to do different
     * things if its the first, last, or somewhere in the middle. 
     */

    if (item == cache->top)
    {
	/* Adjust top */
	cache->top = cache->element[item].next;
	cache->element[cache->top].prev = BAD_LINK;
    }
    else if (item == cache->bottom)
    {
	/* Adjust bottom */
	cache->bottom = cache->element[item].prev;
	cache->element[cache->bottom].next = -1;
    }
    else
    {
	/* Item in the middle */
	prev = cache->element[item].prev;
	next = cache->element[item].next;
	cache->element[prev].next = next;
	cache->element[next].prev = prev;
    }
}

/* dcache_update_dentry_timestamp
 *
 * updates the timestamp of the dcache entry
 *
 * returns 0 on success, -1 on failure
 */
static int dcache_update_dentry_timestamp(dcache_entry* entry) 
{
    int ret = 0;

    /* Update the timestamp */
    ret = gettimeofday(&entry->tstamp_valid,NULL);
    if (ret < 0)
    {
	return(-1);
    }

    entry->tstamp_valid.tv_sec += PINT_DCACHE_TIMEOUT;

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
