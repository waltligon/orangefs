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
#include <db.h>

#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "dbpf-bstream.h"
#include "gossip.h"
#include "quicklist.h"
#include "dbpf-open-cache.h"
#include "pvfs2-internal.h"

#define OPEN_CACHE_SIZE 64

struct open_cache_entry
{
    int ref_ct;
    enum open_ref_type type;

    TROVE_coll_id coll_id;
    TROVE_handle handle;
    int fd;

    struct qlist_head queue_link;
};

/* "used_list" is for active objects (ref_ct > 0) */
static QLIST_HEAD(used_list);
/* "unused_list" is for inactive objects (ref_ct == 0) that we are still
 * holding open in case someone asks for them again soon
 */
static QLIST_HEAD(unused_list);
/* "free_list" is just a list of cache entries that have not been filled in,
 * can be used at any time for new cache entries
 */
static QLIST_HEAD(free_list);
static gen_mutex_t cache_mutex = GEN_MUTEX_INITIALIZER;
static struct open_cache_entry prealloc[OPEN_CACHE_SIZE];

static int open_fd(
    int *fd, 
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    int create_flag);

inline static struct open_cache_entry * dbpf_open_cache_find_entry(
    struct qlist_head * list, 
    const char * list_name,
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    enum open_ref_type type);

void dbpf_open_cache_initialize(void)
{
    int i = 0;

    gen_mutex_lock(&cache_mutex);

    /* run through preallocated cache elements to initialize
     * and put them on the free list
     */
    if (OPEN_CACHE_SIZE == 0)
    {
	gossip_err("Warning: dbpf_open_cache disabled.\n");
    }

    for (i = 0; i < OPEN_CACHE_SIZE; i++)
    {
        prealloc[i].type = -1;
	qlist_add(&prealloc[i].queue_link, &free_list);
    }

    gen_mutex_unlock(&cache_mutex);
}

static void dbpf_open_cache_entries_finalize(
    struct qlist_head * list);

void dbpf_open_cache_finalize(void)
{
    gen_mutex_lock(&cache_mutex);

    /* close any open fd or db references */

    dbpf_open_cache_entries_finalize(&used_list);
    dbpf_open_cache_entries_finalize(&unused_list);
    dbpf_open_cache_entries_finalize(&free_list);

    gen_mutex_unlock(&cache_mutex);
}

int dbpf_open_cache_get(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    int create_flag,
    enum open_ref_type type,
    struct open_cache_ref* out_ref)
{
    struct qlist_head *tmp_link;
    struct open_cache_entry* tmp_entry = NULL;
    int found = 0;
    int ret = 0;

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache_get: called\n");

    gen_mutex_lock(&cache_mutex);

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	"dbpf_open_cache_get: type: %d | create_flag: %d\n",
                 (int)type, create_flag);

    /* check already opened objects first, reuse ref if possible */

    tmp_entry = dbpf_open_cache_find_entry(
        &used_list, "used list", coll_id, handle, type);
    if(!tmp_entry)
    {
        tmp_entry = dbpf_open_cache_find_entry(
            &unused_list, "unused list", coll_id, handle, type);
    }

    out_ref->fd = -1;

    if (tmp_entry)
    {
	if ((type & DBPF_OPEN_BSTREAM) && (tmp_entry->fd < 0))
	{
	    ret = open_fd(&(tmp_entry->fd), coll_id, handle, create_flag);
	    if (ret < 0)
	    {
		gen_mutex_unlock(&cache_mutex);
		return ret;
	    }
            out_ref->fd = tmp_entry->fd;
	}

	out_ref->internal = tmp_entry;
	tmp_entry->ref_ct++;

	/* remove the entry and place it at the used head (assuming it
	 * will be referenced again soon)
	 */
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG, "dbpf_open_cache_get: "
                     "moving to (or reordering in) used list.\n");
	qlist_del(&tmp_entry->queue_link);
	qlist_add(&tmp_entry->queue_link, &used_list);

	gen_mutex_unlock(&cache_mutex);
	return 0;
    }

    /* if we fall through to this point, then the object was not found
     * in the cache. In order of priority we will now try: free list,
     * unused_list, and then bypass cache
     */
    if (!qlist_empty(&free_list))
    {
	tmp_link = free_list.next;
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	qlist_del(&tmp_entry->queue_link);
	found = 1;
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: resetting entry from free list.\n");
    }

    /* anything in unused list (still open, but ref_ct == 0)? */
    if (!found && !qlist_empty(&unused_list))
    {
	tmp_link = unused_list.next;
	tmp_entry = qlist_entry(
            tmp_link, struct open_cache_entry, queue_link);
	qlist_del(&tmp_entry->queue_link);
	found = 1;
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: resetting entry from unused list.\n");

	if (tmp_entry->type == DBPF_OPEN_BSTREAM && tmp_entry->fd > -1)
	{
	    DBPF_CLOSE(tmp_entry->fd);
	    tmp_entry->fd = -1;
	}
    }
   
    if (found)
    {
	/* have an entry to work with; fill in and place in used list */
	tmp_entry->ref_ct = 1;
	tmp_entry->coll_id = coll_id;
	tmp_entry->handle = handle;

	if (type == DBPF_OPEN_BSTREAM)
	{
	    ret = open_fd(&(tmp_entry->fd), coll_id, handle, create_flag);
	    if (ret < 0)
	    {
		qlist_add(&tmp_entry->queue_link, &free_list);
		gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                             "dbpf_open_cache_get: could not open "
                             "(ret=%d)\n", ret);

		gen_mutex_unlock(&cache_mutex);
		return ret;
	    }
            out_ref->fd = tmp_entry->fd;
	}

	out_ref->internal = tmp_entry;
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: moving to used list.\n");
	qlist_add(&tmp_entry->queue_link, &used_list);
	gen_mutex_unlock(&cache_mutex);
	return 0;
    }

    /* if we reach this point the entry wasn't cached _and_ would
     * could not create a new entry for it (cache exhausted).  In this
     * case just open the file and hand out a reference that will not
     * be cached
     */
    
    out_ref->fd = -1;

    if (type & DBPF_OPEN_BSTREAM)
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_get: missed cache entirely.\n");
	ret = open_fd(&(out_ref->fd), coll_id, handle, create_flag);
	if (ret < 0)
	{
	    gen_mutex_unlock(&cache_mutex);
	    return ret;
	}
    }

    out_ref->internal = NULL;
    gen_mutex_unlock(&cache_mutex);

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache_get: returning 0\n");

    return 0;
}
    
void dbpf_open_cache_put(
    struct open_cache_ref* in_ref)
{
    struct open_cache_entry* tmp_entry = NULL;
    int move = 0;

    gen_mutex_lock(&cache_mutex);

    /* handle cached entries */
    if(in_ref->internal)
    {
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

	if(move)
	{
	    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
		"dbpf_open_cache_put: move to unused list.\n");
	    qlist_add_tail(&tmp_entry->queue_link, &unused_list);
	}
    }
    else
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_put: uncached entry.\n");
	/* this wasn't cached; go ahead and close up */
	if(in_ref->fd > -1)
	{
	    DBPF_CLOSE(in_ref->fd);
	    in_ref->fd = -1;
	}
    }
    gen_mutex_unlock(&cache_mutex);
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
    struct qlist_head* scratch;
    int tmp_error = 0;
    DB_ENV *envp = NULL;

    if ((envp = dbpf_getdb_env()) == NULL)
    {
        return TROVE_ENOMEM;
    }
    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache_remove: called\n");

    gen_mutex_lock(&cache_mutex);

    /* for error checking for now, let's make sure that this object is _not_
     * in the used list (we shouldn't be able to delete while another thread
     * or operation has an fd/db open)
     */

    /* TODO: remove this search later when we have more confidence */
    qlist_for_each(tmp_link, &used_list)
    {
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	if ((tmp_entry->handle == handle) &&
            (tmp_entry->coll_id == coll_id))
	{
	    assert(0);
	}
    }

    /* see if the item is in the unused list (ref_ct == 0) */    
    qlist_for_each_safe(tmp_link, scratch, &unused_list)
    {
	tmp_entry = qlist_entry(tmp_link, struct open_cache_entry,
	    queue_link);
	if ((tmp_entry->type == DBPF_OPEN_BSTREAM &&
             (tmp_entry->handle == handle) &&
             (tmp_entry->coll_id == coll_id)))
        {
            qlist_del(&tmp_entry->queue_link);
            found = 1;
            break;
        }
    }

    if (found)
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_remove: unused entry.\n");
	if (tmp_entry->type == DBPF_OPEN_BSTREAM &&
            tmp_entry->fd > -1)
	{
	    DBPF_CLOSE(tmp_entry->fd);
	    tmp_entry->fd = -1;
	}
	qlist_add(&tmp_entry->queue_link, &free_list);
    }
    else
    {
	gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
	    "dbpf_open_cache_remove: uncached entry.\n");
    }

    tmp_error = 0;

    DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
                              my_storage_p->name, coll_id, llu(handle));

    ret = DBPF_UNLINK(filename);
    if ((ret != 0) && (errno != ENOENT))
    {
	tmp_error = -trove_errno_to_trove_error(errno); 
    }

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG, "Unlinked filename: "
                 "(ret=%d, errno=%d)\n%s\n", ret, errno, filename);

    gen_mutex_unlock(&cache_mutex);

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache_remove: returning %d\n", tmp_error);

    return tmp_error;
}

static int open_fd(
    int *fd, 
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    int create_flag)
{
    char filename[PATH_MAX] = {0};

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache open_fd: opening fd %llu, create: %d\n",
                 llu(handle), create_flag);

    DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
			      my_storage_p->name, coll_id, llu(handle));

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache open_fd: filename: %s\n", filename);

    *fd = DBPF_OPEN(filename, O_RDWR, 0);

    if ((*fd < 0) && (errno == ENOENT) && create_flag)
    {
	*fd = DBPF_OPEN(filename, O_RDWR|O_CREAT|O_EXCL, TROVE_DB_MODE);
    }
    return ((*fd < 0) ? -trove_errno_to_trove_error(errno) : 0);
}

static void dbpf_open_cache_entries_finalize(struct qlist_head *list)
{
    struct qlist_head * list_entry;
    struct open_cache_entry * entry; 
    qlist_for_each(list_entry, list)
    {
        entry = qlist_entry(list_entry, struct open_cache_entry, queue_link);
        if(entry->type == DBPF_OPEN_BSTREAM && entry->fd > -1)
        {
	    DBPF_CLOSE(entry->fd);
	    entry->fd = -1;
	}
    }
}

inline static struct open_cache_entry * dbpf_open_cache_find_entry(
    struct qlist_head * list, 
    const char * list_name,
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    enum open_ref_type type)
{
    struct qlist_head *tmp_link;
    struct open_cache_entry *tmp_entry = NULL;

    qlist_for_each(tmp_link, list)
    {
	tmp_entry = qlist_entry(
            tmp_link, struct open_cache_entry, queue_link);
        if(type == tmp_entry->type &&
           type == DBPF_OPEN_BSTREAM &&
           (tmp_entry->handle == handle) &&
           (tmp_entry->coll_id == coll_id))
        {
            gossip_debug(
                GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                "dbpf_open_cache_get: "
                "found bstream entry in %s.\n", list_name);
            return tmp_entry;
        }
    }

    return NULL;
}

            
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
