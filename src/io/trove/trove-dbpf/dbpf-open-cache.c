/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* LRU style cache for file descriptors and/or db references */
/* note that the cache is fixed size.  If we have have more active fd and/or
 * db references than will fit in the cache, then the overflow references
 * will all get new fds that are closed on put
 */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-bstream.h"
#include "gossip.h"
#include "quicklist.h"
#include "dbpf-open-cache.h"

#define OPEN_CACHE_SIZE 64

struct open_cache_entry
{
    int ref_ct;
    TROVE_coll_id coll_id;
    TROVE_handle handle;
    int fd;
    struct qlist_head queue_link;
};

static QLIST_HEAD(free_list);
static QLIST_HEAD(used_list);
static QLIST_HEAD(unused_list);
static gen_mutex_t free_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t used_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t unused_mutex = GEN_MUTEX_INITIALIZER;
static struct open_cache_entry prealloc[OPEN_CACHE_SIZE];

void dbpf_open_cache_initialize(void)
{
    int i;

    /* run through preallocated cache elements to initialize
     * and put them on the free list
     */
    for(i=0; i<OPEN_CACHE_SIZE; i++)
    {
	prealloc[i].fd = -1;
	qlist_add(&prealloc[i].queue_link, &free_list);
    }

    return;
}

void dbpf_open_cache_finalize(void)
{
    /* TODO: fill this in */
    return;
}

int dbpf_open_cache_get(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    int create_flag,
    enum open_ref_type type,
    struct open_cache_ref* out_ref)
{
    struct qlist_head* tmp_link;
    struct open_cache_entry* tmp_entry = NULL;
    int found = 0;
    char filename[PATH_MAX];
    int ret;

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	"dbpf_open_cache_get: type: %d\n", (int)type);
    
    /* we haven't implemented db support yet */
    /* TODO: remove this eventually */
    assert(type == DBPF_OPEN_FD);

    /* check the list of already opened objects first, reuse ref if possible */
    gen_mutex_lock(&used_mutex);
    gen_mutex_lock(&unused_mutex);

    qlist_for_each(tmp_link, &used_list)
    {
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	if(tmp_entry->handle == handle && tmp_entry->coll_id == coll_id)
	{
	    found = 1;
	    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
		"dbpf_open_cache_get: found entry in used list.\n");
	    break;
	}
    }

    if(!found)
    {
	qlist_for_each(tmp_link, &unused_list)
	{
	    tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
		queue_link);
	    if(tmp_entry->handle == handle && tmp_entry->coll_id == coll_id)
	    {
		found = 1;
		gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
		    "dbpf_open_cache_get: found entry in unused list.\n");
		break;
	    }
	}
    }

    /* found a match */
    if(found)
    {
	if((type & DBPF_OPEN_FD) && (tmp_entry->fd < 0))
	{
	    /* need to open bstream */
	    DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
				      my_storage_p->name, coll_id, Lu(handle));
	    tmp_entry->fd = DBPF_OPEN(filename, O_RDWR, 0);
	    if(tmp_entry->fd < 0 && errno == ENOENT && create_flag)
	    {
		tmp_entry->fd = DBPF_OPEN(filename,
		    O_RDWR|O_CREAT|O_EXCL,
		    TROVE_DB_MODE);
	    }
	    
	    if(tmp_entry->fd < 0)
	    {
		ret = -trove_errno_to_trove_error(errno);
		gen_mutex_unlock(&unused_mutex);
		gen_mutex_unlock(&used_mutex);
		return(ret);
	    }
	}

	out_ref->fd = tmp_entry->fd;
	out_ref->internal = tmp_entry;
	tmp_entry->ref_ct++;
	/* remove the entry and place it at the used head (assuming it will be
	 * referenced again soon) 
	 */
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: moving to (or reordering in) used list.\n");
	qlist_del(&tmp_entry->queue_link);
	qlist_add(&tmp_entry->queue_link, &used_list);

	gen_mutex_unlock(&unused_mutex);
	gen_mutex_unlock(&used_mutex);
	return(0);
    }

    gen_mutex_unlock(&unused_mutex);
    gen_mutex_unlock(&used_mutex);

    /* if we fall through to this point, then the object was not found in
     * the cache. 
     */
    /* In order of priority we will now try: free list, unused_list, and 
     * then bypass cache
     */

    /* anything in the free list? */
    gen_mutex_lock(&free_mutex);
    if(!qlist_empty(&free_list))
    {
	tmp_link = free_list.next;
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	qlist_del(&tmp_entry->queue_link);
	found = 1;
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: resetting entry from free list.\n");
    }
    gen_mutex_unlock(&free_mutex);
 
    /* anything in unused list (still open, but ref_ct == 0)? */
    gen_mutex_lock(&unused_mutex);
    if(!found && !qlist_empty(&unused_list))
    {
	tmp_link = unused_list.next;
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	qlist_del(&tmp_entry->queue_link);
	found = 1;
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: resetting entry from unused list.\n");

	if(tmp_entry->fd > 0)
	{
	    DBPF_CLOSE(tmp_entry->fd);
	}
    }
    gen_mutex_unlock(&unused_mutex);
   
    if(found)
    {
	/* we have an entry to work with; fill it in and place in used list */
	tmp_entry->ref_ct = 1;
	tmp_entry->coll_id = coll_id;
	tmp_entry->handle = handle;
	if(type & DBPF_OPEN_FD)
	{
	    /* need to open bstream */
	    DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
				      my_storage_p->name, coll_id, Lu(handle));
	    tmp_entry->fd = DBPF_OPEN(filename, O_RDWR, 0);
	    if(tmp_entry->fd < 0 && errno == ENOENT && create_flag)
	    {
		tmp_entry->fd = DBPF_OPEN(filename,
		    O_RDWR|O_CREAT|O_EXCL,
		    TROVE_DB_MODE);
	    }
	    
	    if(tmp_entry->fd < 0)
	    {
		ret = -trove_errno_to_trove_error(errno);
		gen_mutex_lock(&free_mutex);
		qlist_add(&tmp_entry->queue_link, &free_list);
		gen_mutex_unlock(&free_mutex);
		gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
		    "dbpf_open_cache_get: could not open.\n");
		return(ret);
	    }
	}
	out_ref->fd = tmp_entry->fd;
	out_ref->internal = tmp_entry;
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: moving to used list.\n");
	gen_mutex_lock(&used_mutex);
	qlist_add(&tmp_entry->queue_link, &used_list);
	gen_mutex_unlock(&used_mutex);
	return(0);
    }

    /* if we reach this point the entry wasn't cached _and_ would could not
     * create a new entry for it (cache exhausted).  In this case just open
     * the file and hand out a reference that will not be cached
     */
    if(type & DBPF_OPEN_FD)
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: missed cache entirely.\n");
	/* need to open bstream */
	DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
				  my_storage_p->name, coll_id, Lu(handle));
	out_ref->fd = DBPF_OPEN(filename, O_RDWR, 0);
	if(out_ref->fd < 0 && errno == ENOENT && create_flag)
	{
	    out_ref->fd = DBPF_OPEN(filename,
		O_RDWR|O_CREAT|O_EXCL,
		TROVE_DB_MODE);
	}
	
	if(out_ref->fd < 0)
	{
	    ret = -trove_errno_to_trove_error(errno);
	    return(ret);
	}
    }
    out_ref->internal = NULL;
    return(0);
}
    
void dbpf_open_cache_put(
    struct open_cache_ref* in_ref)
{
    struct open_cache_entry* tmp_entry = NULL;
    int move = 0;

    /* handle cached entries */
    if(in_ref->internal)
    {
	gen_mutex_lock(&used_mutex);
	tmp_entry = in_ref->internal;
	tmp_entry->ref_ct--;

	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_put: cached entry.\n");

	if(tmp_entry->ref_ct == 0)
	{
	    /* put this in unused list since ref ct hit zero */
	    move = 1;
	    qlist_del(&tmp_entry->queue_link);	    
	}
	gen_mutex_unlock(&used_mutex);

	if(move)
	{
	    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
		"dbpf_open_cache_put: move to unused list.\n");
	    gen_mutex_lock(&unused_mutex);
	    qlist_add_tail(&tmp_entry->queue_link, &unused_list);
	    gen_mutex_unlock(&unused_mutex);
	}
    }
    else
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_put: uncached entry.\n");
	/* this wasn't cached; go ahead and close up */
	if(in_ref->fd > 0)
	{
	    DBPF_CLOSE(in_ref->fd);
	}
    }

    return;
}

int dbpf_open_cache_remove(
    TROVE_coll_id coll_id,
    TROVE_handle handle)
{
    struct qlist_head* tmp_link;
    struct open_cache_entry* tmp_entry = NULL;
    int found = 0;
    char filename[PATH_MAX];
    int ret = -1;

    /* for error checking for now, let's make sure that this object is _not_
     * in the used list (we shouldn't be able to delete while another thread
     * or operation has an fd/db open)
     */

    /* TODO: remove this search later when we have more confidence */
    gen_mutex_lock(&used_mutex);
    qlist_for_each(tmp_link, &used_list)
    {
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	if(tmp_entry->handle == handle && tmp_entry->coll_id == coll_id)
	{
	    assert(0);
	}
    }
    gen_mutex_unlock(&used_mutex);

    /* see if the item is in the unused list (ref_ct == 0) */    
    gen_mutex_lock(&unused_mutex);
    qlist_for_each(tmp_link, &unused_list)
    {
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	if(tmp_entry->handle == handle && tmp_entry->coll_id == coll_id)
	{
	    qlist_del(&tmp_entry->queue_link);
	    found = 1;
	}
    }
    gen_mutex_unlock(&unused_mutex);

    if(found)
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_remove: unused entry.\n");
	DBPF_CLOSE(tmp_entry->fd);
	tmp_entry->fd = -1;
	gen_mutex_lock(&free_mutex);
	qlist_add(&tmp_entry->queue_link, &free_list);
	gen_mutex_unlock(&free_mutex);
    }
    else
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_remove: uncached entry.\n");
    }

    DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
                              my_storage_p->name, coll_id, Lu(handle));

    ret = DBPF_UNLINK(filename);
    if (ret != 0 && errno != ENOENT)
    {
	return -trove_errno_to_trove_error(errno); 
    }
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
