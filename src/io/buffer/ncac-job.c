/* This file defines the horseworker for each particular type of jobs.  */

#include <stdio.h>
#include <stdlib.h>


#include "internal.h"
#include "state.h"
#include "aiovec.h"
#include "cache.h"

#include "ncac-trove.h"

extern struct cache_stack global_cache_stack;
extern struct inode *inode_arr[1000];

/* internal functions */
static inline struct extent * NCAC_find_get_ext(NCAC_req_t *ncac_req, unsigned long index);
static inline struct extent * NCAC_alloc_ext(NCAC_req_t *ncac_req);
static inline struct extent * NCAC_alloc_ext_wait(NCAC_req_t *ncac_req);
static inline int NCAC_add_to_cache(struct extent * extent,unsigned long index, NCAC_req_t *ncac_req);


/* do a read job.
 * return: <0 error code
 *         0: ok
 * ncac_req->status shows the current status of the job
 * ncac_req->error shows the current error of the job if any.
 *
 * Lock stuff: A design choice has been made to do locks as follows:
 *     1) each inode has a lock;
 *     2) each cache stack has a lock (many inodes may share a same cache stack).
 * To avoid lock calls on each extent, we had a sort of "big" lock across jobs on an inode.
 * During a job processing, if the cache stack is touched, the job should acquire the cache
 * stack lock. So the lock order is:
 *     inode lock
 *             ----> cache stack lock
 *             ----> release cache stack lock
 *     release inode lock
 * 
 *  So, we make a tradeoff between the number of lock calls and the granularity of lock.
 */

int NCAC_do_a_read_job(struct NCAC_req *ncac_req)
{
    int ret;
    int seg, cnt;
    int rcomm=0;

	inode_lock(&ncac_req->mapping->lock);
	
    /* only one contiguous segment */
    if ( !ncac_req->offcnt ) { 
        ret = NCAC_do_one_piece_read( ncac_req, ncac_req->pos, 
									  ncac_req->size, ncac_req->cbufoff, 
									  ncac_req->cbufsize, ncac_req->cbufhash,
 									  ncac_req->cbufflag, 
									  ncac_req->cbufrcnt,
									  ncac_req->cbufwcnt,
									  &cnt);
        if ( ret < 0) {
            ncac_req->error = NCAC_JOB_PROCESS_ERR;
            ncac_req->status = NCAC_ERR_STATUS;

			inode_unlock( &ncac_req->mapping->lock );

            return ret;
        }
    }else{

        /* Handle each contiguous piece one by one. */
        
        cnt = 0;
        for (seg = 0; seg < ncac_req->offcnt; seg ++) {
            ret = NCAC_do_one_piece_read( ncac_req, ncac_req->offvec[seg],
                                          ncac_req->sizevec[seg],
                                          ncac_req->cbufoff + cnt, 
                                          ncac_req->cbufsize + cnt, 
                                          ncac_req->cbufhash + cnt, 
                                          ncac_req->cbufflag + cnt,
									      ncac_req->cbufrcnt + cnt,
									      ncac_req->cbufwcnt + cnt, &seg);
            if ( ret < 0) {
            	ncac_req->error = NCAC_JOB_PROCESS_ERR;
            	ncac_req->status = NCAC_ERR_STATUS;

			    inode_unlock( &ncac_req->mapping->lock );

            	return ret;
            }
            cnt += seg;
        }
    }

	inode_unlock(&ncac_req->mapping->lock);

    for (seg = 0; seg < ncac_req->cbufcnt; seg ++)
         if (ncac_req->cbufflag[seg] == 1) rcomm++;
         
    if (rcomm == ncac_req->cbufcnt) ncac_req->status = NCAC_BUFFER_COMPLETE;
    else if (!rcomm) ncac_req->status = NCAC_REQ_SUBMITTED;
    else ncac_req->status = NCAC_PARTIAL_PROCESS;

    return 0;
}

/* NCAC_do_one_piece_read(): handle one contiguous block.
 * return:
 *    < 0: error
 *    = 0: no error
 *    at the same time, ncac_req->error shows error no if any.
 *    ncac_req->status shows the status of this one piece.
 *
 *    TODO: 1) use gang lookup;
 *          2) allocate contiguous extents from a bigger buffer
 */ 

int NCAC_do_one_piece_read(NCAC_req_t *ncac_req, PVFS_offset pos, 
                           PVFS_size size, char **cbufoff, 
                           PVFS_size *cbufsize, struct extent *cbufhash[],
                           int *cbufflag,
						   int *cbufrcnt,
						   int *cbufwcnt,
						   int *cnt)
{
    unsigned long index;
	unsigned int offset, nr;
    struct extent *cached_ext;
    struct extent *extent;
    int error;
    int ret;

    struct aiovec aiovec_arr, *aiovec;
    int ioreq;

    int cbufcnt;
    int toread=0;
    int slots;
    int i, j;
    
    PVFS_offset oldpos = pos;


    aiovec = &aiovec_arr;
    aiovec_init(aiovec);

    cbufcnt = (pos+size+ NCAC_dev.extsize -1)/NCAC_dev.extsize - pos/NCAC_dev.extsize;
    *cnt = cbufcnt;

    cached_ext = NULL;
    index =  pos >> NCAC_dev.extlog2;
    
    DPRINT("one_piece_read: pos=%Ld, sindex=%ld  cnt=%d\n", pos, index, cbufcnt);
    for (i=0; i< cbufcnt; i++) {

        if ( cbufhash[i] ) {

            DPRINT("Read recheck: cbufrcnt[%d]=%d, cbufwcnt[%d]=%d, e.rcmp=%d, e.wcmp=%d, extent flags=%lx (cbufflag=%d)\n", i, cbufrcnt[i], i, cbufwcnt[i], cbufhash[i]->rcmp, cbufhash[i]->wcmp, cbufhash[i]->flags, cbufflag[i]);


            /* still previous writes pending  on this */
            if ( cbufwcnt[i] >  cbufhash[i]->wcmp )  {
	            index ++;
				pos += nr;
				continue;
            }

            if ( cbufwcnt[i] <  cbufhash[i]->wcmp ) {
                NCAC_error("Error: wcnt should not be less than cmp\n");
	            index ++;
				pos += nr;
				continue;
            }

            extent = cbufhash[i];
            offset = cbufoff[i] - extent->addr;
			nr = cbufsize[i];

            DPRINT("recheck: offset=%p, nr=%d extent=%p\n", cbufoff[i], nr, extent);
		    error = NCAC_extent_read_access_recheck(ncac_req, extent, offset, nr);
		    if (error < 0){
			    ncac_req->error = error;	
                NCAC_error("NCAC_extent_read_access_recheck error  extent=%p\n", extent);
			    return error;
		    }
            cbufflag[i] = error;

            DPRINT("Read recheck: cbufrcnt[%d]=%d, cbufwcnt[%d]=%d, e.rcmp=%d, e.wcmp=%d, extent flags=%lx (cbufflag=%d)\n", i, cbufrcnt[i], i, cbufwcnt[i], cbufhash[i]->rcmp, cbufhash[i]->wcmp, cbufhash[i]->flags, cbufflag[i]);
            
            index ++;
            pos += nr;
            continue;
        }


        offset = (unsigned long)pos & (NCAC_dev.extsize -1);
        nr = cbufsize[i];

		/* try to find an cached extent. If cached, the reference count is
		 * added.
		 */
        extent = NCAC_find_get_ext(ncac_req, index);

        if (extent == NULL) {
            goto no_cached_extent;
        }


		/* the extent is cached */
		error = NCAC_extent_read_access(ncac_req, extent, offset, nr);
		if (error < 0){
			extent_ref_release( extent );	
			ncac_req->error = error;	
			return error;
		}

        DPRINT("index=%ld is cached: extent flags:%lx reads=%d, writes=%d, rcmp=%d, wcmp=%d\n", index, extent->flags, extent->reads, extent->writes, extent->rcmp, extent->wcmp);

        cbufflag[i] = error; /* maybe ready, maybe not */
        cbufhash[i] = extent;

        cbufrcnt[i] = extent->reads;
        cbufwcnt[i] = extent->writes;

        cbufoff[i] += (unsigned long)extent->addr;

		/* prepare for the next extent */
		index += 1;
        pos += nr;

        continue; /* continue for the next extent */

no_cached_extent:
        /* the extent was not cached. we need to create a new extent. */

        if (!cached_ext) {
            cached_ext = NCAC_alloc_ext_wait(ncac_req);
            if (cached_ext) {
            	NCAC_extent_first_read_access(ncac_req, cached_ext);
				cached_ext->index = index;
				cached_ext->mapping = ncac_req->mapping;
			}
		}

        extent = cached_ext;
        cached_ext = NULL;

        cbufhash[i] = extent;
        if ( extent ){
            cbufoff[i] += (unsigned long)extent->addr;
            cbufflag[i] = 0; /* not ready for communication */

            cbufhash[i]->ioreq = INVAL_IOREQ;

            toread ++;

            cbufrcnt[i] = extent->reads;
            cbufwcnt[i] = extent->writes;
        }
     
		/* prepare for the next extent */
		index += 1;
        pos += nr;
    }

    if ( !toread ) return 0;

    pos = oldpos;
    for (i = 0; i < cbufcnt; i++ ) {

        if ( cbufhash[i] && PageBlank(cbufhash[i]) ) {

	    	 slots = aiovec_add(aiovec, cbufhash[i], pos, cbufsize[i], cbufoff[i], cbufsize[i]);

             DPRINT("do_a_job: going to read (%Ld %Ld) to %p\n", pos, cbufsize[i], cbufoff[i]);
             pos += cbufsize[i];

             if (!slots){
                 ret = NCAC_aio_read_ext(ncac_req->coll_id, ncac_req->handle, ncac_req->context_id, aiovec, &ioreq);
		         if ( ret < 0 ) {
                     ncac_req->error = NCAC_TROVE_AIO_REQ_ERR;
			         ncac_req->status = NCAC_ERR_STATUS;
					
					 NCAC_error("aio_read_ext error\n");

        		     aiovec_init(aiovec);
                     return ret;
		         }else{
                     aiovec->extent_array[0]->ioreq = ioreq;
                     extent = aiovec->extent_array[0];
                     extent->ioreq_next = extent;
			         for (j = 1; j < aiovec_count(aiovec); j ++) {
                         aiovec->extent_array[j]->ioreq = ioreq;

                         aiovec->extent_array[j-1]->ioreq_next = aiovec->extent_array[j];
			         }

			         aiovec->extent_array[aiovec_count(aiovec)-1]->ioreq_next = aiovec->extent_array[0]; 
                 }

                 DPRINT("do_a_job: aio_read cnt=%d\n", aiovec_count(aiovec));
        		 aiovec_init(aiovec);
             }
		}
	}

    DPRINT("do_one_piece_read: aio_read cbufcnt=%d, cnt=%d\n", cbufcnt, aiovec_count(aiovec));

    ioreq = INVAL_IOREQ;

    if (aiovec_count(aiovec)){
        ret = NCAC_aio_read_ext(ncac_req->coll_id, ncac_req->handle, ncac_req->context_id, aiovec, &ioreq);
	    if ( ret < 0 ) {
            ncac_req->error = NCAC_TROVE_AIO_REQ_ERR;
	        ncac_req->status = NCAC_ERR_STATUS;
            aiovec_init(aiovec);

            NCAC_error("do_one_piece_read: NCAC_aio_read_ext error\n");

            return ret;
	    }else{
            aiovec->extent_array[0]->ioreq = ioreq;
            extent = aiovec->extent_array[0];
            extent->ioreq_next = extent;
	        for (i= 1; i < aiovec_count(aiovec); i ++) {
                aiovec->extent_array[i]->ioreq = ioreq;

                aiovec->extent_array[i-1]->ioreq_next = aiovec->extent_array[i];
            }

            aiovec->extent_array[aiovec_count(aiovec)-1]->ioreq_next = aiovec->extent_array[0]; 
        }
        aiovec_init(aiovec);
    }

    /* add to cache */
    for (i = 0; i < cbufcnt; i++ ) {
        if ( cbufhash[i] && PageBlank(cbufhash[i]) ) {
             ClearPageBlank(cbufhash[i]);
           	 ret = NCAC_add_to_cache(cbufhash[i], cbufhash[i]->index, ncac_req);

           	 if ( ret < 0 ) {
                 ncac_req->error = NCAC_CACHE_ERR;

                 NCAC_error("do_one_piece_read: add_to_cache error: index=%ld\n", index);

               	 return ret;
           	}
		}
	}

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
    int ret;
    int seg, cnt;
    int rcomm=0;

	inode_lock(&ncac_req->mapping->lock);

    /* only one contiguous segment */
    if ( !ncac_req->offcnt ) { 
        ret = NCAC_do_one_piece_write(  ncac_req, ncac_req->pos, 
										ncac_req->size, 
										ncac_req->cbufoff, ncac_req->cbufsize, 
										ncac_req->cbufhash, ncac_req->cbufflag, 
									    ncac_req->cbufrcnt,
									    ncac_req->cbufwcnt,
										&cnt );
        if ( ret < 0) {
            ncac_req->error = NCAC_JOB_PROCESS_ERR;
            ncac_req->status = NCAC_ERR_STATUS;

			inode_unlock(&ncac_req->mapping->lock);

            return ret;
        }
    }else{

        /* Handle each contiguous piece one by one. */
        
        cnt = 0;
        for (seg = 0; seg < ncac_req->offcnt; seg ++) {
            ret = NCAC_do_one_piece_write( ncac_req, ncac_req->offvec[seg],
                                                ncac_req->sizevec[seg],
                                                ncac_req->cbufoff + cnt, 
                                                ncac_req->cbufsize + cnt, 
                                                ncac_req->cbufhash + cnt, 
                                                ncac_req->cbufflag + cnt, 
									    		ncac_req->cbufrcnt + cnt,
									    		ncac_req->cbufwcnt + cnt,
												&seg );
            if ( ret < 0) {
            	ncac_req->error = NCAC_JOB_PROCESS_ERR;
            	ncac_req->status = NCAC_ERR_STATUS;

				inode_unlock(&ncac_req->mapping->lock);

            	return ret;
            }
            cnt += seg;
        }
    }

	inode_unlock(&ncac_req->mapping->lock);

    for (seg = 0; seg < ncac_req->cbufcnt; seg ++)
         if (ncac_req->cbufflag[seg] == 1 ) rcomm++;
         
    if (rcomm == ncac_req->cbufcnt) ncac_req->status = NCAC_BUFFER_COMPLETE;
    else if (!rcomm) ncac_req->status = NCAC_REQ_SUBMITTED;
    else ncac_req->status = NCAC_PARTIAL_PROCESS;

    return 0;

} /* end of  do_a_write_job */


/* NCAC_do_one_piece_write(): handle one contiguous block write.
 * return:
 *    < 0: error
 *    = 0: no error
 *    at the same time, ncac_req->error shows error no if any.
 *    ncac_req->status shows the status of this one piece.
 *
 *    TODO: 1) use gang lookup;
 *          2) allocate contiguous extents from a bigger buffer
 */ 

int NCAC_do_one_piece_write(NCAC_req_t *ncac_req, PVFS_offset pos, 
                            PVFS_size size, char **cbufoff, 
                            PVFS_size *cbufsize, struct extent *cbufhash[],
                            int *cbufflag, 
							int *cbufrcnt,
							int *cbufwcnt,
							int *cnt)
{
    unsigned long index, offset, nr;
    struct extent *cached_ext;
    struct extent *extent;
    int error;
    int ret;

    struct aiovec *aiovec;
    int ioreq;

    int cbufcnt;
    int i;

    /* each inode has an aiovec */
    aiovec = get_aiovec(ncac_req);
    aiovec_init(aiovec);

    cbufcnt = (pos+size+ NCAC_dev.extsize -1)/NCAC_dev.extsize - pos/NCAC_dev.extsize;
    *cnt = cbufcnt;

    cached_ext = NULL;
    index =  pos >> NCAC_dev.extlog2;
    
    for (i=0; i< cbufcnt; i++) {
		nr = cbufsize[i];

        if ( cbufhash[i] ) { /* extent is avaiable. */

            DPRINT("Write recheck: cbufrcnt[%d]=%d, cbufwcnt[%d]=%d, e.rcmp=%d, e.wcmp=%d, extent flags=%lx (cbufflag=%d)\n", i, cbufrcnt[i], i, cbufwcnt[i], cbufhash[i]->rcmp, cbufhash[i]->wcmp, cbufhash[i]->flags, cbufflag[i]);

			/* ugly here: 1: ok, 2: read-modify-write */
            if ( cbufflag[i] == 1 ) { /* has been assigned */
	            index ++;
				pos += nr;
				continue;
            }


            /* Are previous reads and writes pending on this? 
             * "+1" to exclude the request itself.
             */
            if ( cbufwcnt[i] >  cbufhash[i]->wcmp + 1 || 
                 cbufrcnt[i] >  cbufhash[i]->rcmp  )  {
	            index ++;
				pos += nr;
				continue;
            }

            /* this is only for error check */
            if ( cbufwcnt[i] <  cbufhash[i]->wcmp + 1 ||
                 cbufrcnt[i] <  cbufhash[i]->rcmp ) {
                NCAC_error("Error: r/wcnt should not be less than r/wcmp\n");
	            index ++;
				pos += nr;
				continue;
            }

            /* no other pending read or writes on this extent */
            extent = cbufhash[i];
            offset = cbufoff[i] - extent->addr;

		    error = NCAC_extent_write_access_recheck(ncac_req, extent, offset, nr);
		    if (error < 0){
			    ncac_req->error = error;	
                fprintf(stderr, "NCAC_extent_read_access_recheck error  extent=%p\n", extent);
			    return error;
		    }
            cbufflag[i] = error;

            DPRINT("Write recheck: cbufrcnt[%d]=%d, cbufwcnt[%d]=%d, e.rcmp=%d, e.wcmp=%d, extent flags=%lx (cbufflag=%d)\n", i, cbufrcnt[i], i, cbufwcnt[i], cbufhash[i]->rcmp, cbufhash[i]->wcmp, cbufhash[i]->flags, cbufflag[i]);

			
            
            index ++;
            pos += nr;
            continue;
        }


        offset = (unsigned long)pos & (NCAC_dev.extsize -1);

        extent = NCAC_find_get_ext(ncac_req, index);

        if (extent == NULL) {
            goto no_cached_extent;
        }

		/* the extent is cached */
		error = NCAC_extent_write_access(ncac_req, extent, offset, nr);
		if (error < 0){
			ncac_req->error = error;	
			return error;
		}

        cbufflag[i] = error; /* 1 for ready, 0 for not ready */
        cbufhash[i] = extent;


        /* how many reads and writes pending on this extent before
         * this request */
        cbufrcnt[i] = extent->reads;
        cbufwcnt[i] = extent->writes;

        DPRINT("index=%ld is cached: extent flags:%lx cbufflag=%d, reads=%d, writes=%d, rcmp=%d, wcmp=%d\n", index, extent->flags, cbufflag[i], extent->reads, extent->writes, extent->rcmp, extent->wcmp);

        cbufoff[i] += (unsigned long)extent->addr;

		/* prepare for the next extent */
		index += 1;
        pos += nr;

        continue; /* continue for the next extent */

no_cached_extent:
        /* the extent was not cached. we need to create a new extent. */

        if (!cached_ext) {
            cached_ext = NCAC_alloc_ext_wait(ncac_req);
            if (!cached_ext) {
		        cbufflag[i] = 0;
            }else{
            	NCAC_extent_first_write_access(ncac_req, cached_ext);

				cached_ext->index = index;
				cached_ext->mapping = ncac_req->mapping;

		        cbufflag[i] = 1;

		        cbufrcnt[i] = cached_ext->reads;
        		cbufwcnt[i] = cached_ext->writes;


	            /* deal with read, modify and write. In the case if the write size is
		         * not the whole write unit, we should read it first, modify it, and 
		         * then write.
		         */
                if ( cbufflag[i] && ( cbufoff[i] || cbufsize[i] <  NCAC_dev.extsize )){
			        DPRINT("--------do read-modify-write\n");

			        do_read_for_rmw(ncac_req->coll_id, 
									ncac_req->handle, 
									ncac_req->context_id, 
									cached_ext, 
									pos, 
									cbufoff[i], 
									cbufsize[i], 
									&ioreq);
			        mark_extent_rmw_lock(cached_ext, ioreq); 
			        cbufflag[i] = 2;
		        }
                cbufoff[i] += (unsigned long)cached_ext->addr;

            	ret = NCAC_add_to_cache(cached_ext,index, ncac_req);

            	if (ret) {
		            cbufflag[i] = 0;
                    cbufhash[i] = 0;
               		ncac_req->error = NCAC_CACHE_ERR;
                	return ret;
            	}
				ncac_req->nr_dirty ++;

                DPRINT("index=%ld is NOT cached: extent flags:%lx reads=%d, writes=%d, rcmp=%d, wcmp=%d, cbufoff=%p, flag=%d, size=%Ld pos=%Ld\n", index, cached_ext->flags, cached_ext->reads, cached_ext->writes, cached_ext->rcmp, cached_ext->wcmp, cbufoff[i], cbufflag[i], cbufsize[i], pos);
			}
		}

        extent = cached_ext;
        cached_ext = NULL;

        cbufhash[i] = extent;
     
		/* prepare for the next extent */
		index += 1;
        pos += nr;
    }

	return 0;

} /* end of do_one_piece_write */

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

/* some internal functions */

/* NCAC_find_get_ext(): try to find an extent from the inode cache tree.
 * This operation is protected by the inode lock. The caller should acquire
 * the inode lock.
 */
static inline struct extent * NCAC_find_get_ext(NCAC_req_t *ncac_req, unsigned long index)
{
    struct extent *avail;

    avail = lookup_cache_item(ncac_req->mapping, index);

#if 0 /* take this back when we have finer lock */
	if ( avail ) { /* add its reference count to prevent disappearance */
		extent_ref_get( avail );
	}
#endif

    return avail;

}


/* NCAC_alloc_ext(): get a new extent
 * The caller should have an inode lock
 */

static inline struct extent * NCAC_alloc_ext(NCAC_req_t *ncac_req)
{
    struct extent *new = NULL;
	struct cache_stack *cache;
    char *buf;

	cache = ncac_req->mapping->cache_stack;

    if ( !list_empty( &cache->free_extent_list ) ) {

		cache_lock( &cache->lock);

        new = get_free_extent_list_item( &(cache->free_extent_list) );

		cache_unlock(&cache->lock);
	}	

    if (!new) return NULL;

    buf = new->addr;
    memset(new, 0, sizeof(struct extent));
    new->addr = buf;
    SetPageBlank(new);
    fprintf(stderr, "new extent:%p, flags:%lx\n", new, new->flags);
	return new;
}


/* NCAC_alloc_ext_wait(): if no extent is avaiable, discard some if possible. 
 * Lock problem is a little more difficult than others since this funtion may
 * interact with the inode resource and the cache resource.
 * 
 * This function is called by functions which holds its inode lock,
 * only cache stack lock is needed.
 * .
 */
static inline struct extent * NCAC_alloc_ext_wait(NCAC_req_t *ncac_req)
{
    struct extent *new = NULL;
	struct cache_stack *cache;

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

	cache_lock( &cache->lock);

    ret = try_to_discard_extents(cache, DELT_DISCARD_NUM); 
    if ( ret < 0 ) {
        ncac_req->error = ret;
		cache_unlock(&cache->lock);
        return NULL;
	}

    new = get_free_extent_list_item( &(ncac_req->mapping->cache_stack->free_extent_list) );

	cache_unlock(&cache->lock);

	if ( !new ) return NULL;

    buf = new->addr;
    memset(new, 0, sizeof(struct extent));
    new->addr = buf;
    new->ioreq = INVAL_IOREQ;
    SetPageBlank(new);
    DPRINT("new extent:%p, flags:%lx, ioreq=%d\n", new, new->flags, new->ioreq);
	return new;
}



static inline int NCAC_add_to_cache(struct extent * extent,unsigned long index, NCAC_req_t *ncac_req)
{
    int ret;

    ret = add_cache_item(extent, ncac_req->mapping, index);

    return ret;
}

static inline int NCAC_read_ext(struct extent *extent, PVFS_offset offset, unsigned long nr)
{
   extent->ioreq = 0;
   return 0;
}
