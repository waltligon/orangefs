/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* LRU style cache for file descriptors */
/* note that the cache is fixed size.  If we have have more active fd and/or
 * db references than will fit in the cache, then the overflow references
 * will all get new fds that are closed on put
 */

#define XOPEN_SOURCE 500

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <db.h>
#include <dirent.h>

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

    TROVE_coll_id coll_id;
    TROVE_handle handle;
    int fd;
    enum open_cache_open_type type;

    struct qlist_head queue_link;
};

struct unlink_context
{
    pthread_t       thread_id;
    pthread_mutex_t mutex;
    pthread_cond_t  data_available;
    struct qlist_head global_list; 
};

struct file_struct
{
    struct qlist_head list_link;   
    char *pathname;
};

static struct unlink_context dbpf_unlink_context;
static void* unlink_bstream(void *context);
static int fast_unlink(
    const char *pathname, 
    TROVE_coll_id coll_id, 
    TROVE_handle handle);

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
    enum open_cache_open_type type);

static void close_fd(
    int fd, 
    enum open_cache_open_type type);

inline static struct open_cache_entry * dbpf_open_cache_find_entry(
    struct qlist_head * list, 
    const char * list_name,
    TROVE_coll_id coll_id,
    TROVE_handle handle);

void dbpf_open_cache_initialize(void)
{
    int i = 0, ret = 0;

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
        prealloc[i].fd = -1;
	qlist_add(&prealloc[i].queue_link, &free_list);
    }

    gen_mutex_unlock(&cache_mutex);

    /* Initialize and create the worker thread for threaded deletes */
    INIT_QLIST_HEAD(&dbpf_unlink_context.global_list);
    pthread_mutex_init(&dbpf_unlink_context.mutex, NULL);
    pthread_cond_init(&dbpf_unlink_context.data_available, NULL);
    ret = pthread_create(&dbpf_unlink_context.thread_id, NULL, unlink_bstream, (void*)&dbpf_unlink_context);
    if(ret)
    {
        gossip_err("dbpf_open_cache_initialize: failed [%d]\n", ret);
        return;
    }
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

    pthread_cancel(dbpf_unlink_context.thread_id);
}

/**
 * The dbpf open cache is used primarily to manage open
 * file descriptors to bstream files on IO servers.  PVFS
 * currently uses a lazy style of creating the actual datafiles for
 * bstreams.  Only on the first write to a bstream is the file
 * actually created (opened with O_CREAT).  This means that if a
 * read of a bstream that hasn't been written should somehow occur,
 * an ENOENT error will be returned immediately, instead of allowing
 * a read to EOF (of a zero-byte file).  For us, this is ok, since
 * the client gets the size of the bstream in the getattr before doing
 * any IO.  All that being said, the open_cache_get call needs to
 * behave differently based on the desired operation:  reads on
 * files that don't exist should return ENOENT, but writes on files
 * that don't exist should create and open the file.
 */
int dbpf_open_cache_get(
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    enum open_cache_open_type type,
    struct open_cache_ref* out_ref)
{
    struct qlist_head *tmp_link;
    struct open_cache_entry* tmp_entry = NULL;
    int found = 0;
    int ret = 0;

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache_get: called\n");

    gen_mutex_lock(&cache_mutex);

    /* check already opened objects first, reuse ref if possible */

    tmp_entry = dbpf_open_cache_find_entry(
        &used_list, "used list", coll_id, handle);
    if(!tmp_entry)
    {
        tmp_entry = dbpf_open_cache_find_entry(
            &unused_list, "unused list", coll_id, handle);
    }

    out_ref->fd = -1;

    if (tmp_entry)
    {
	if (tmp_entry->fd < 0)
	{
	    ret = open_fd(&(tmp_entry->fd), coll_id, handle, type);
	    if (ret < 0)
	    {
		gen_mutex_unlock(&cache_mutex);
		return ret;
	    }
            tmp_entry->type = type;
	}
        out_ref->fd = tmp_entry->fd;
        out_ref->type = type;

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

        assert(out_ref->fd > 0);
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

	if (tmp_entry->fd > -1)
	{
            close_fd(tmp_entry->fd, tmp_entry->type);
	    tmp_entry->fd = -1;
	}
    }
   
    if (found)
    {
	/* have an entry to work with; fill in and place in used list */
	tmp_entry->ref_ct = 1;
	tmp_entry->coll_id = coll_id;
	tmp_entry->handle = handle;

        ret = open_fd(&(tmp_entry->fd), coll_id, handle, type);
        if (ret < 0)
        {
            qlist_add(&tmp_entry->queue_link, &free_list);
            gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                         "dbpf_open_cache_get: could not open "
                         "(ret=%d)\n", ret);

            gen_mutex_unlock(&cache_mutex);
            return ret;
        }
        tmp_entry->type = type;
        out_ref->type = type;
        out_ref->fd = tmp_entry->fd;

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

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
        "dbpf_open_cache_get: missed cache entirely.\n");
    ret = open_fd(&(out_ref->fd), coll_id, handle, type);
    if (ret < 0)
    {
        gen_mutex_unlock(&cache_mutex);
        return ret;
    }
    out_ref->type = type;

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
            close_fd(in_ref->fd, in_ref->type);
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
    int tmp_error = 0;
    struct qlist_head* scratch;

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
	if ((tmp_entry->handle == handle) &&
             (tmp_entry->coll_id == coll_id))
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
	if (tmp_entry->fd > -1)
	{
            close_fd(tmp_entry->fd, tmp_entry->type);
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

    ret = fast_unlink(filename, coll_id, handle);

    if ((ret != 0) && (errno != ENOENT))
    {
        tmp_error = -trove_errno_to_trove_error(errno); 
    }

    gen_mutex_unlock(&cache_mutex);

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache_remove: returning %d\n", tmp_error);

    return tmp_error;
}

static int open_fd(
    int *fd, 
    TROVE_coll_id coll_id,
    TROVE_handle handle,
    enum open_cache_open_type type)
{
    int flags = 0;
    int mode = 0;
    char filename[PATH_MAX] = {0};

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache open_fd: opening fd %llu\n",
                 llu(handle));

    DBPF_GET_BSTREAM_FILENAME(filename, PATH_MAX,
			      my_storage_p->name, coll_id, llu(handle));

    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
                 "dbpf_open_cache open_fd: filename: %s\n", filename);

    flags = O_RDWR;

    if(type == DBPF_FD_BUFFERED_WRITE ||
       type == DBPF_FD_DIRECT_WRITE)
    {
        flags |= O_CREAT;
        mode = TROVE_FD_MODE;
    }

    if(type == DBPF_FD_DIRECT_WRITE || type == DBPF_FD_DIRECT_READ)
    {
        flags |= O_DIRECT;
    }

    *fd = DBPF_OPEN(filename, flags, mode);
    return ((*fd < 0) ? -trove_errno_to_trove_error(errno) : 0);
}

static void dbpf_open_cache_entries_finalize(struct qlist_head *list)
{
    struct qlist_head * list_entry;
    struct qlist_head * scratch;
    struct open_cache_entry * entry; 
    qlist_for_each_safe(list_entry, scratch, list)
    {
        entry = qlist_entry(list_entry, struct open_cache_entry, queue_link);
        if(entry->fd > -1)
        {
            close_fd(entry->fd, entry->type);
	    entry->fd = -1;
	}
        qlist_del(&entry->queue_link);
    }
    /* Cancel the deletion thread */
    pthread_cancel(dbpf_unlink_context.thread_id);
}

inline static struct open_cache_entry * dbpf_open_cache_find_entry(
    struct qlist_head * list, 
    const char * list_name,
    TROVE_coll_id coll_id,
    TROVE_handle handle)
{
    struct qlist_head *tmp_link;
    struct open_cache_entry *tmp_entry = NULL;

    qlist_for_each(tmp_link, list)
    {
	tmp_entry = qlist_entry(
            tmp_link, struct open_cache_entry, queue_link);
        if((tmp_entry->handle == handle) &&
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

int fast_unlink(const char *pathname, TROVE_coll_id coll_id, TROVE_handle handle)
{
    int ret;
    struct file_struct *tmp_item;
    
    tmp_item = (struct file_struct *) malloc(sizeof(struct file_struct));
    if(!tmp_item)
    {
        gossip_err("Unable to allocate memory for file_struct [%d].\n", errno);
        return -TROVE_ENOMEM;
    }
    tmp_item->pathname = malloc(PATH_MAX);
    if(!tmp_item->pathname)
    {
        gossip_err("Unable to allocate memory for pathname[%d].\n", errno);
        free(tmp_item);
        return -TROVE_ENOMEM;
    }
    DBPF_GET_STRANDED_BSTREAM_FILENAME(tmp_item->pathname, PATH_MAX,
                                       my_storage_p->name, 
                                       coll_id,
                                       llu(handle));
    
    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG, 
                 "Renaming [%s] to [%s] for threaded delete.\n", pathname, tmp_item->pathname);

    ret = rename(pathname, tmp_item->pathname);
    if(ret != 0)
    {
        gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG, 
            "Warning: During unlink, the rename failed on file [%s] with errno [%d] strerr [%s].\n", 
            pathname, errno, strerror(errno));
        free(tmp_item->pathname);
        free(tmp_item);
        return ret;
    }
    
    /* Add to the queue */
    pthread_mutex_lock(&dbpf_unlink_context.mutex); 
    qlist_add_tail(&tmp_item->list_link, &dbpf_unlink_context.global_list);
    pthread_cond_signal(&dbpf_unlink_context.data_available);
    pthread_mutex_unlock(&dbpf_unlink_context.mutex); 
    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG, 
        "Added [%s] to the queue.\n", tmp_item->pathname);
    
    return(0);
}

static void* unlink_bstream(void *context)
{
    struct unlink_context *loc_context = (struct unlink_context *) context;
    int ret;
    time_t start_time;
    struct qlist_head *tmp_item;
    struct file_struct *tmp_st;
  
    while(1)
    {
        pthread_mutex_lock(&loc_context->mutex);
        /* If there is no work to do, go into a condition wait */
        if(qlist_empty(&loc_context->global_list))
        {
            pthread_cond_wait(&loc_context->data_available, &loc_context->mutex);
        }
        
        if(!qlist_empty(&loc_context->global_list))
        {
            tmp_item = loc_context->global_list.next;
            qlist_del(tmp_item);
            pthread_mutex_unlock(&loc_context->mutex);
        }
        else /* Condition triggered without items in qlist */
        {
            pthread_mutex_unlock(&loc_context->mutex);
            gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG, 
                "Unlink condition triggered when qlist empty\n");
            continue; /* Enter while loop again */
        }
    
        tmp_st = qlist_entry(tmp_item, struct file_struct, list_link);
        time(&start_time);
        ret = DBPF_UNLINK(tmp_st->pathname);
        gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG, 
            "Unlinked filename: (ret=%d, errno=%d, elapsed-time=%ld(secs) )\n%s\n", 
            ret, errno, (time(NULL) - start_time), tmp_st->pathname);
        free(tmp_st->pathname);
        free(tmp_st);
    }

    pthread_exit(&loc_context->thread_id);
    return NULL;
}

static void close_fd(
    int fd, 
    enum open_cache_open_type type)
{
    gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
        "dbpf_open_cache closing fd %d of type %d\n", fd, type);
    DBPF_CLOSE(fd);
}

void clear_stranded_bstreams(TROVE_coll_id coll_id)
{
    char path_name[PATH_MAX];
    DIR *current_dir = NULL;
    struct dirent *current_dirent = NULL;
    struct stat file_info;
    struct file_struct *tmp_item;

    DBPF_GET_STRANDED_BSTREAM_DIRNAME(path_name, PATH_MAX,
                                      my_storage_p->name, coll_id);

    /* remove entries in the stranded bstreams directory */
    current_dir = opendir(path_name);
    if(current_dir)
    {
        while((current_dirent = readdir(current_dir)))
        {
            if((strcmp(current_dirent->d_name, ".") == 0) ||
               (strcmp(current_dirent->d_name, "..") == 0))
            {
                continue;
            }
            tmp_item = (struct file_struct *) malloc(sizeof(struct file_struct));
            if(!tmp_item)
            {
                gossip_err("Unable to allocate memory for file_struct [%d].\n", errno);
                return;
            }
            tmp_item->pathname = malloc(PATH_MAX);
            if(!tmp_item->pathname)
            {
                gossip_err("Unable to allocate memory for pathname[%d].\n", errno);
                free(tmp_item);
                return;
            }
            snprintf(tmp_item->pathname, PATH_MAX, "%s/%s", path_name,
                     current_dirent->d_name);
            if(stat(tmp_item->pathname, &file_info) < 0)
            {
                gossip_err("error doing stat on bstream entry\n");
                continue;
            }
            assert(S_ISREG(file_info.st_mode));
            /* Add to the queue */

            pthread_mutex_lock(&dbpf_unlink_context.mutex);
            qlist_add_tail(&tmp_item->list_link, &dbpf_unlink_context.global_list);
            pthread_cond_signal(&dbpf_unlink_context.data_available);
            pthread_mutex_unlock(&dbpf_unlink_context.mutex);
            gossip_debug(GOSSIP_DBPF_OPEN_CACHE_DEBUG,
              "Added [%s] to the queue.\n", tmp_item->pathname);
        }
        closedir(current_dir);
    }
    else
    {   
        gossip_err("Unable to open standed bstream directory [%s] to "
          "perform initialization stranded bstream cleanup", path_name);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
