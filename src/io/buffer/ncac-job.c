/* This file defines the horseworkers for each particular type of jobs.  */

#include <stdio.h>
#include <stdlib.h>


#include "internal.h"
#include "state.h"
#include "aiovec.h"
#include "cache.h"

#include "ncac-trove.h"

extern struct cache_stack global_cache_stack;

/* internal functions */
static inline struct extent *find_extent(NCAC_req_t *ncac_req,
                       unsigned long index);
static inline struct extent *allocate_extent(NCAC_req_t *ncac_req, 
                       int flag);
static inline int free_extent(NCAC_req_t *ncac_req,
                       struct extent *extent);
static inline int init_extent_read(NCAC_req_t *ncac_req,
                struct extent *extent, PVFS_offset foffset, PVFS_size size);
static inline void set_extent_read_pending(struct extent *extent);
static inline int check_extent_read(NCAC_req_t *ncac_req, struct extent *extent);
static inline void increase_read_reference(struct extent *extent);
static inline int add_extent_to_cache(struct extent * extent,
         unsigned long index, NCAC_req_t *ncac_req, int policy);

/* do a read job.
 * return: < 0 error code
 *         0: ok
 * ncac_req->status shows the current status of the job
 * ncac_req->error shows the current error of the job if any.
 *
 * Lock stuff: A design choice has been made to do locks as follows:
 *     1) each inode has a lock;
 *     2) each cache stack has a lock (many inodes may share a same 
 *	cache stack).
 * To avoid lock calls on each extent, we had a sort of "big" lock 
 * across jobs on an inode. During a job processing, if the cache stack 
 * is touched, the job should acquire the cache stack lock. So the lock 
 * order is:
 *	inode lock
 *             ----> cache stack lock
 *             ----> release cache stack lock
 *  release inode lock
 * 
 *  So, we make a tradeoff between the number of lock calls and the 
 *  granularity of lock.
 */

int NCAC_do_a_read_job(struct NCAC_req *ncac_req)
{
	int ret;
    struct extent **cbufhash;
    PVFS_offset *foff;
    int *cbufflag;
    struct extent *new_extent;
    struct extent *last_extent;

    unsigned long index;
    int comcnt, readcnt;
    int i;
    

    /* even there are "comcnt" communication buffers, the
     * number of extents needed may be less.
     */
    comcnt = ncac_req->cbufcnt;
    fprintf(stderr, "NCAC_do_a_read_job: enter (comcnt=%d)\n", comcnt);

    cbufhash = ncac_req->cbufhash;
    foff = ncac_req->foff;
    cbufflag = ncac_req->cbufflag;

    inode_lock (&ncac_req->mapping->lock);

    last_extent = NULL;
    for (i=0; i<comcnt; i++){
        if ( NULL == cbufhash[i] ){
            index = foff[i] >> NCAC_dev.extlog2;
		    new_extent = find_extent(ncac_req, index);
            if ( NULL == new_extent ){ /* not cached */
			    new_extent= allocate_extent(ncac_req,BLOCKING_EXTENT_ALLOC);
			    if ( new_extent ){
				    new_extent->index = index;
				    new_extent->mapping = ncac_req->mapping;
                    new_extent->ioreq = INVAL_IOREQ;

				    ret = init_extent_read(ncac_req, new_extent,
                                         foff[i], NCAC_dev.extsize);
                    if ( ret < 0 ) {
				        NCAC_error("init_extent_read error ext:%p\n",
                                 new_extent);

                        free_extent(ncac_req, new_extent);
	                    inode_unlock (&ncac_req->mapping->lock);
                        return ret;
                    }
                    add_extent_to_cache(new_extent, index, ncac_req,
                                        LRU_POLICY);
                    set_extent_read_pending(new_extent);
                    cbufhash[i] = new_extent;
                }
            }else{ /* cached */
                cbufhash[i] = new_extent;
                hit_cache_item(new_extent, LRU_POLICY);
            }

            /* only one reference for each request */
            if ( cbufhash[i] && cbufhash[i] != last_extent )
                increase_read_reference(cbufhash[i]); 
            last_extent = cbufhash[i];
        }

        if ( cbufhash[i] ){
            ret = 1;
            if ( PageReadPending(cbufhash[i]) ){
                fprintf(stderr, "extent:%p ioreq:%Ld\n", cbufhash[i], Ld(cbufhash[i]->ioreq));
                ret = check_extent_read(ncac_req, cbufhash[i]);
                if (ret < 0){
				    ncac_req->error = ret;	
				    NCAC_error("check_read_pending extent=%p\n", cbufhash[i]);

                    inode_unlock (&ncac_req->mapping->lock);
				    return ret;
			    }
            }
			cbufflag[i] = ret;
		}
    }

	inode_unlock (&ncac_req->mapping->lock);

    readcnt = 0;
    for (i=0; i<comcnt; i++){
        if (ncac_req->cbufflag[i]) readcnt++;
    }

    if (readcnt == ncac_req->cbufcnt) ncac_req->status = NCAC_BUFFER_COMPLETE;
    else if (!readcnt) ncac_req->status = NCAC_REQ_SUBMITTED;
    else ncac_req->status = NCAC_PARTIAL_PROCESS;

    fprintf(stderr, "NCAC_do_a_read_job: exit\n");
	return 0;
}

/* do a write job.
 * return: <0 error code
 *         0: ok
 * ncac_req->status shows the current status of the job
 * ncac_req->error shows the current error of the job if any.
 */

int NCAC_do_a_write_job(struct NCAC_req *ncac_req)
{
    return 0;

} /* end of  do_a_write_job */


int NCAC_do_a_query_job(struct NCAC_req *ncac_req)
{
    NCAC_error("NCAC_do_a_query_job: not implemented yet\n");
    return 0;
}

int NCAC_do_a_demote_job(struct NCAC_req *ncac_req)
{
    NCAC_error("NCAC_do_a_demote_job: not implemented yet\n");
    return 0;
}

int NCAC_do_a_sync_job(struct NCAC_req *ncac_req)
{
    NCAC_error("NCAC_do_a_sync_job: not implemented yet\n");
    return 0;
}


/* 
 * find_extent(): try to find an extent from the inode cache tree.
 * This operation is protected by the inode lock. The caller should 
 * acquire the inode lock.
 */
static inline struct extent *find_extent(NCAC_req_t *ncac_req, 
                                        unsigned long index)
{
    struct extent *avail;

    avail = lookup_cache_item(ncac_req->mapping, index);
    return avail;
}


/* 
 * allocate_extent(): get a new extent. The caller should have 
 * an inode lock. The flag is either BLOCKING_EXTENT_ALLOC or
 * NONBLOCKING_EXTENT_ALLOC.
 * If the "flag" is BLOCKING_EXTENT_ALLOC, if no extent is avaiable, 
 * discard some if possible. Lock problem is a little more difficult 
 * than others since this funtion may interact with the inode resource 
 * and the cache resource.
 * 
 * This function is called by functions which holds its inode lock,
 * only cache stack lock is needed.
 */

static inline struct extent *allocate_extent(NCAC_req_t *ncac_req, int flag)
{
    struct extent *new = NULL;
	struct cache_stack *cache;
    int shrinked;

    char *buf;
    int ret;

	cache = ncac_req->mapping->cache_stack;

    if ( !list_empty( &cache->free_extent_list ) ) {
		cache_lock( &cache->lock);
        new = get_free_extent_list_item( &(cache->free_extent_list) );
	    cache_unlock(&cache->lock);

        if ( new ) {
    		buf = new->addr;
   			memset(new, 0, sizeof(struct extent));
    		new->addr = buf;
    		SetPageBlank(new);
    		DPRINT("new extent:%p, flags:%lx\n", new, new->flags);
			return new;
		}
    }

    /* No free extent so far */
    if ( BLOCKING_EXTENT_ALLOC == flag ){

		cache_lock( &cache->lock);
        ret = shrink_cache(cache, DELT_DISCARD_NUM, LRU_POLICY, &shrinked); 
        if ( ret < 0 ) {
            ncac_req->error = ret;
		    cache_unlock(&cache->lock);
            return NULL;
        }
        new = get_free_extent_list_item( &(ncac_req->mapping->cache_stack->free_extent_list) );
	    cache_unlock(&cache->lock);

	    if ( !new ) return NULL;
        else {
    		buf = new->addr;
   			memset(new, 0, sizeof(struct extent));
    		new->addr = buf;
    		SetPageBlank(new);
    		DPRINT("new extent:%p, flags:%lx\n", new, new->flags);
			return new;
	    }
    }

    return NULL;
}

/* add it later 
 * free_extent: return an extent to a list
 */
static inline int free_extent(NCAC_req_t *ncac_req,struct extent *extent)
{
    return 0;
}

/* 
 * init_extent_read: initiate trove request to read an extent. The
 * file offset is "foffset", and the size is "size".
 */
static inline int init_extent_read(NCAC_req_t *ncac_req, 
                   struct extent *extent, PVFS_offset foffset, PVFS_size size)
{
    int ret;
    PVFS_id_gen_t ioreq;


    ret = init_io_read(ncac_req->coll_id, ncac_req->handle, 
            ncac_req->context_id, foffset, size, extent->addr, &ioreq);
    if ( ret < 0 ) {
        NCAC_error("init_io_read error\n");
        return ret;
    }
    extent->ioreq = ioreq;
    fprintf(stderr, "init_extent_read: foff:%Ld, size:%Ld, extent:%p, opid:%Ld\n", Ld(foffset), Ld(size), extent, Ld(ioreq));
    return 0;
}

static inline void set_extent_read_pending(struct extent *extent)
{
    ClearPageBlank(extent);
	SetPageReadPending(extent);
}

static inline int check_extent_read(NCAC_req_t *ncac_req, struct extent *extent)
{
    int ret;

    ret = NCAC_check_ioreq(extent);
    if ( ret > 0 ){
         ClearPageReadPending(extent);
        SetPageClean(extent);
        return 1;
    }
    return 0;
}

static inline void increase_read_reference(struct extent *extent)
{
    extent->reads ++;
    return;
}

static inline void increase_write_reference(struct extent *extent)
{
    extent->reads ++;
    return;
}

static inline int add_extent_to_cache(struct extent * extent,
            unsigned long index, NCAC_req_t *ncac_req, int policy)
{
    int ret;

    ret = add_cache_item(extent, ncac_req->mapping, index, policy);

    return ret;
}
