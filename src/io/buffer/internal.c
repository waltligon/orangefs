#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "internal.h"
#include "state.h"
#include "flags.h"
#include "aiovec.h"
#include "cache.h"
#include "ncac-job.h"

extern struct inode *inode_arr[1000];
extern struct NCAC_dev  NCAC_dev;

/* This file contains NCAC internal functions. */


static inline struct inode *get_inode(PVFS_fs_id, PVFS_handle , PVFS_context_id);
static inline int NCAC_rwjob_prepare_one_piece(PVFS_offset pos, PVFS_size size, char ** cbufoff, PVFS_size * cbufsize, struct extent **cbufhash);

/* get_internal_req(): get a internal request structure from the free
 * list. To avoid dynamic allocation, for the timebeing, I hard code
 * the total number of outstanding requests. In the future, a dynamic
 * one could be taken. That is, if all static ones have been used, 
 * extra requests can be allocated on demand.
 *
 */

static inline struct NCAC_req * get_internal_req_lock( PVFS_fs_id fsid, PVFS_handle hndl)
{

    NCAC_req_t *req=NULL;
	struct list_head *new;

	list_lock(&NCAC_dev.req_list_lock); 

	if ( list_empty(&NCAC_dev.free_req_list) ) return NULL;

	new = NCAC_dev.free_req_list.next;
	list_del_init(new);

	list_unlock(&NCAC_dev.req_list_lock); 

	req = list_entry(new->prev, NCAC_req_t, list);

    return req;  
}


/* add a request into the tail of a list exclusively. */
void NCAC_list_add_tail_lock(struct list_head *new, struct list_head *head, NCAC_lock *lock)
{
    list_lock(lock);

    list_add_tail(new, head);

    list_unlock(lock);

}

/* del an entry from its list */
void NCAC_list_del_lock(struct list_head *entry, NCAC_lock *lock)
{
    list_lock(lock);

    list_del(entry);

    list_unlock(lock);
}

/* read an entry without mark from a list and mark the entry. */
void NCAC_read_request_from_list_lock(struct list_head *head, NCAC_lock *lock, NCAC_req_t ** ncac_req_ptr)
{
    struct list_head *pos;
    NCAC_req_t *req = NULL;

    list_lock(lock);

    for (pos = head->next; pos != head; pos = pos->next) {
            req = list_entry( pos, NCAC_req_t, list);
        if ( !req->read_out ) {
            req->read_out = 1;
            break;
        }
    }

    list_unlock(lock);

    *ncac_req_ptr = req;
}



/* build internal read/write requests */
NCAC_req_t *NCAC_rwreq_build( NCAC_desc_t *desc, NCAC_optype optype)
{
    void *iovec;
    NCAC_req_t *ncac_req;

    ncac_req = get_internal_req_lock(desc->coll_id, desc->handle);
    if (ncac_req == NULL) { /* run out of ncac request resources */
        fprintf(stderr, "no free request\n");
        return NULL;
    }

    ncac_req->coll_id 		= desc->coll_id;
    ncac_req->handle  		= desc->handle;
    ncac_req->context_id  	= desc->context_id;

    /* inode */
    ncac_req->mapping = get_inode(desc->coll_id, desc->handle, desc->context_id);
    /* inode aiovec */
    ncac_req->aiovec  = &(ncac_req->mapping->aiovec);
    ncac_req->nr_dirty = 0;

    if ( desc->buffer ) { /* buffer read or not */
        if ( optype == NCAC_GEN_READ ) 
            ncac_req->optype = NCAC_BUF_READ;
        else ncac_req->optype = NCAC_BUF_WRITE;

        ncac_req->usrbuf  = desc->buffer;
        ncac_req->usrlen  = desc->len;
    }else{
        if ( optype == NCAC_GEN_READ ) 
            ncac_req->optype = NCAC_READ;
        else ncac_req->optype = NCAC_WRITE;

        ncac_req->usrbuf  = NULL;
        ncac_req->usrlen  = 0;
    }

    ncac_req->written = 0;  /* size finished */

    if ( desc->stream_array_count == 1 ) { /* one segment case */

        ncac_req->pos     = desc->stream_offset_array[0];
        ncac_req->size    = desc->stream_size_array[0];
        ncac_req->offcnt  = 0;    /* no vector */
        ncac_req->offvec  = NULL; /* no vector */
        ncac_req->sizevec = NULL; /* no vector */

    }else{ /* a list of <off, len> tuples */

        /* I do want to avoid this malloc */

        iovec = (void*) malloc( desc->stream_array_count*(sizeof(PVFS_offset)+sizeof(PVFS_size)) );
        if (iovec == NULL ) {
            ncac_req->status = NCAC_NO_MEM;
            return ncac_req;
        }

        ncac_req->offcnt = desc->stream_array_count;
        ncac_req->offvec  = (PVFS_offset*)iovec;
        ncac_req->sizevec = (PVFS_size *)( (unsigned long)iovec + desc->stream_array_count*sizeof(PVFS_offset) );

        /* copy user reuqest's stuff here */

    }

    /* success */
    ncac_req->status = NCAC_OK;
    return ncac_req;
}

/*
 * NCAC_rwjob_prepare(): does three things:
 * (1) allocate resource; caculate index, offset, and length; 
 * (2) put the request in the interanl job list
 * (3) make progress of the requests in the job list.
 */
int NCAC_rwjob_prepare(NCAC_req_t *ncac_req, NCAC_reply_t *reply )
{
    int bufcnt, cnt;
    char **cbufoff;
    PVFS_size     *cbufsize;
    struct extent **cbufindex;
    int           *cbufflag;
    int 		  *cbufrcnt;
    int			  *cbufwcnt;
    int ret;
    int seg;
    

    /* stream <off, len> --> page info. */

    if ( !ncac_req->offcnt ) { /* only one contiguous segment */

        /* bufcnt: the biggest number of buffers the data could be
         * placed in the cache.
         */
        bufcnt = (ncac_req->pos + ncac_req->size + NCAC_dev.extsize -1)/NCAC_dev.extsize - ncac_req->pos/NCAC_dev.extsize; 
    }else {
        bufcnt = 0;
        for (seg = 0; seg < ncac_req->offcnt; seg ++) 
            bufcnt += (ncac_req->offvec[seg]+ncac_req->sizevec[seg] + NCAC_dev.extsize -1)/NCAC_dev.extsize - ncac_req->offvec[seg]/NCAC_dev.extsize;
    }
   
    /* try to reuse buffer info. arrays if possible. "reserved_cbufcnt" is the
	 * size of the previous request. If the size of the current request is
	 * not larger than the previous one, we reuse the previous resource. Otherwise,
	 * we free the old one and malloc the new one. */

    if ( ncac_req->reserved_cbufcnt < bufcnt ) {
        if ( ncac_req->cbufoff ) free( ncac_req->cbufoff);

        cbufoff  =(char**) malloc( (2*sizeof(char*)+sizeof(PVFS_size)+3*sizeof(int))* bufcnt ); 
        cbufsize =(PVFS_size*)  &cbufoff[bufcnt];
        cbufindex =(struct extent**) &cbufsize[bufcnt];
        cbufflag =(int*) &cbufindex[bufcnt];
        cbufrcnt =(int*) &cbufflag[bufcnt];
        cbufwcnt =(int*) &cbufrcnt[bufcnt];
        

        if ( cbufoff == NULL ) {
            ncac_req->error = -ENOMEM;
            return -ENOMEM;
        }

        ncac_req->cbufoff  = cbufoff;
        ncac_req->cbufsize = cbufsize;
        ncac_req->cbufhash = cbufindex;
        ncac_req->cbufflag = cbufflag;
        ncac_req->cbufrcnt = cbufrcnt;
        ncac_req->cbufwcnt = cbufwcnt;

        ncac_req->reserved_cbufcnt = bufcnt;
    }

    ncac_req->cbufcnt = bufcnt;
    memset(ncac_req->cbufoff, 0, (2*sizeof(char*)+sizeof(PVFS_size)+3*sizeof(int))*ncac_req->reserved_cbufcnt);

    if ( !ncac_req->offcnt ) { /* only one contiguous segment */
        ret = NCAC_rwjob_prepare_one_piece( ncac_req->pos, 
											ncac_req->size, 
											ncac_req->cbufoff, 
											ncac_req->cbufsize, 
											ncac_req->cbufhash);
        if ( ret != bufcnt) {
            fprintf(stderr, "Error: bufcnt error in prepare\n");
            ncac_req->error = NCAC_JOB_PREPARE_ERR;
            ncac_req->status = NCAC_ERR_STATUS;
            return NCAC_JOB_PREPARE_ERR;
        }
    }else{

        /* multiple <off len> tuples. Handle each contiguous piece one
         * by one. */
        
        cnt = 0;
        for (seg = 0; seg < ncac_req->offcnt; seg ++) {
            ret = NCAC_rwjob_prepare_one_piece(ncac_req->offvec[seg],
                                               ncac_req->sizevec[seg],
                                               ncac_req->cbufoff + cnt, 
                                               ncac_req->cbufsize + cnt, 
                                               ncac_req->cbufhash + cnt);
            cnt += ret;
        }
        if (cnt > bufcnt) {
            fprintf(stderr, "Error: bufcnt error in prepare\n");
            ncac_req->error = NCAC_JOB_PREPARE_ERR;
        	ncac_req->status = NCAC_ERR_STATUS;
            return NCAC_JOB_PREPARE_ERR;
        }
    }
         
    /* put the request in the internal job list: thread safe */
   
    NCAC_list_add_tail_lock(&ncac_req->list, &NCAC_dev.prepare_list, &NCAC_dev.req_list_lock);

    ncac_req->status = NCAC_REQ_SUBMITTED;

    DPRINT("NCAC_rwjob_prepare: %p submitted\n", ncac_req);
     
    /* make progress of jobs: thread safe.
	 * Choices here are: 1) do one job; 2) scan the whole list. Choose 1) here. */
    //ret = NCAC_do_jobs(&(NCAC_dev.req_list), &(NCAC_dev.bufcomp_list), &(NCAC_dev.comp_list), &NCAC_dev.req_list_lock); 

    ret = NCAC_do_a_job(ncac_req, &(NCAC_dev.prepare_list), &(NCAC_dev.bufcomp_list), &(NCAC_dev.comp_list), &NCAC_dev.req_list_lock);

    if ( ret < 0 ) {
        ncac_req->error = NCAC_JOB_DO_ERR;
        ncac_req->status = NCAC_ERR_STATUS;
        return ret;
    }

    ncac_req->error  = NCAC_OK;


    return 0;
}


static inline int NCAC_rwjob_prepare_one_piece(PVFS_offset pos, PVFS_size size, char ** cbufoff, PVFS_size * cbufsize, struct extent **cbufhash)
{
    unsigned long offset;
    unsigned long bufcnt, len;
    int seg;
    
    bufcnt = (pos + size + NCAC_dev.extsize -1)/NCAC_dev.extsize - pos/NCAC_dev.extsize; 

    len = 0;

    /* first one */
    offset = (unsigned long)pos & (NCAC_dev.extsize -1); /* within extent */
    cbufoff[0] =(char*) offset; /* add the extent address later */
    cbufsize[0] = NCAC_dev.extsize - offset;
    len += cbufsize[0];
        
    /* middle ones */
    for (seg = 1; seg < bufcnt - 1; seg ++ ) {
        /* add the extent address later */
        cbufoff[seg] = 0;
        cbufsize[seg] = NCAC_dev.extsize;
        len += cbufsize[seg];
    }

    /* last ones */
    if ( bufcnt > 1 ){
        cbufoff[bufcnt-1] = 0;
        cbufsize[bufcnt-1] = size - len;
    }


    return bufcnt;
}

/* NCAC_do_jobs(): this is the workhorse of NCAC.
 * Several things are worth being noted.
 * 1) There are three lists in the NCAC. A request may be in one of
 *    them given a time. It also migrates from one to another.
 *    prepare_list: all submitted requests get first into this list.
 *    bufcomp_list: when a request has all its cache buffers available
 *                  for read, this means all read data are in cache.
 *                  for write, this means all buffers needed to place
 *                  written data are available.
 *    comp_list: when a request does not need cache buffer any more.
 *               There are several cases: for buffered operations, this
 *               means that data have been copied between user buffers
 *               and cache buffers; for non-buffered operations, this
 *               means that the cache consumer has called "cache_req_done"
 *               to notify that communication over buffers has been finished.
 *    
 * 2) trigger:
 *    prepare_list -- (progress engine) --> bufcomp_list -- (cache_req_done)
 *                 --> comp_list
 *    prepare_list -- (progress engine) -->comp_list
 * 
 * 3) NCAC_do_jobs acts as a progress engine and tries to move
 *    requests from prepare_list to bufcompl_list or move them
 *    from prepare_list to comp_list.
 *
 * 4) Order semantics: 
 *    FIFO order is maintained. that is,
 *    for requests from a same client, the request processing order
 *    is the same as the order these requests come into the NCAC.
 * 
 * 5) Locking:
 *    NCAC_do_jobs() could be a thread. We assume that
 *    this thread can be work on the specified list exclusively.
 *    This implies that if multiple "NCAC_do_jobs()" threads exist,
 *    we associate one (or more than one) list(s) to each thread.
 *    Thus, we don't need to lock while in NCAC_do_jobs.
 *
 *    Complication: order semantics??? 
 *    
 * 6) TODO: multiple lists for group ids. That is, we have multiple
 *    prepare_lists and other related lists. One extreme case is that
 *    each <fs_id, handle> has a list.
 *    
 */

int NCAC_do_jobs(struct list_head *prep_list, struct list_head *bufcomp_list, struct list_head *comp_list, NCAC_lock *lock) 
{
    int ret; 
    NCAC_req_t *ncac_req;
        
dojob:
    /* read a request from the prep_list job. When a job is read out 
     * (NOT taken from the list), there is a flag to inidcate that 
     * someone else has read this request out. So get_request_from_list 
     * is always return a request which is not read out by others 
     */    
    
    NCAC_read_request_from_list_lock(prep_list, lock, &ncac_req);

    if (ncac_req) {
        ret = NCAC_do_a_job(ncac_req, prep_list, bufcomp_list, comp_list, lock); 

        ncac_req->read_out = 0;
        if ( ret < 0 ) 
            return ret;

        if ( ncac_req->status == NCAC_BUFFER_COMPLETE || 
             ncac_req->status == NCAC_COMPLETE ) 
            goto dojob; 
    }

    return 0; 
}


/* NCAC_do_a_job(): make progress on a particular request.
 * According to the job optype, a particular job horseworker
 * is called. All horseworkers are implemented in "ncac_job.c".
 */

int NCAC_do_a_job(NCAC_req_t *ncac_req, struct list_head *prep_list, struct list_head *bufcomp_list, struct list_head *comp_list, NCAC_lock *lock)
{
    int ret;
   
    switch (ncac_req->optype){

        /* cached read */
		case NCAC_READ: 

			ret = NCAC_do_a_read_job(ncac_req);
			break;

        /* cached write */
		case NCAC_WRITE: 

			ret = NCAC_do_a_write_job(ncac_req);
			break;
        
		/* cached buffer read */
		case NCAC_BUF_READ:

			ret = NCAC_do_a_bufread_job(ncac_req);
			break;

		/* cached buffer write */
		case NCAC_BUF_WRITE:

			ret = NCAC_do_a_bufwrite_job(ncac_req);
			break;
       
		case NCAC_QUERY:
			ret = NCAC_do_a_query_job(ncac_req);
			break;

		case NCAC_DEMOTE:
			ret = NCAC_do_a_demote_job(ncac_req);
			break;

		case NCAC_SYNC:         
			ret = NCAC_do_a_sync_job(ncac_req);
			break;

        default:
			ret = NCAC_JOB_OPTYPE_ERR;
            fprintf(stderr, "NCAC_do_a_job: unrecognize optype flag\n");
			break;
	}

    if ( ncac_req->status == NCAC_BUFFER_COMPLETE ) {

        NCAC_list_del_lock(&ncac_req->list, lock);

        NCAC_list_add_tail_lock(&ncac_req->list, bufcomp_list, lock); 

    }else if ( ncac_req->status == NCAC_COMPLETE ) 
    {
        NCAC_list_del_lock(&ncac_req->list, lock);
        NCAC_list_add_tail_lock(&ncac_req->list, comp_list, lock); 
    }

	return ret;
}


int NCAC_check_request( int id, struct NCAC_req **ncac_req )
{
    struct NCAC_req *req;
    int ret;

    req = &NCAC_dev.free_req_src[id]; 
    if ( req->status == NCAC_COMPLETE || req->status == NCAC_BUFFER_COMPLETE ) {
    	*ncac_req = req;
        return 0;
    }

	if ( req->status == NCAC_REQ_UNUSED ) {
    	*ncac_req = NULL;
		NCAC_error("NCAC_check_request:no such request");
        return -1;
	}

    ret = NCAC_do_a_job(req, &(NCAC_dev.prepare_list), &(NCAC_dev.bufcomp_list), &(NCAC_dev.comp_list), &NCAC_dev.req_list_lock);

    if ( ret < 0 ) {
		NCAC_error("NCAC_check_request:do a job error (%d)", req->error);
    }
    *ncac_req = req;
    return ret;
}

/* done request(): mark a request is done. Several cases:
 *     NCAC_BUFFER_COMPLETE: pending communication on buffers is done.
 * 					     state transition.
 *     NCAC_COMPLETE: 	nothing with cache buffers.
 *
 * both return ncac_req to the req free list.
 *
 * Tradeoff:  we pre-allocate a list of req structures during initilization.
 * These requests are shared by all flows. So we need to have lock to
 * get and return them to the free list. This is guarded by 
 * NCAC_dev.req_list_lock. We have two benefits at this lock cost.
 * (1) no need to allocate and free request
 * (2) reuse buffer information structure if possible. A lazy free technique
 *     is used to reuse the arrays for buffer information.
 */

int NCAC_done_request( int id )
{
    struct NCAC_req *ncac_req;
    int ret = 0;

    ncac_req = &NCAC_dev.free_req_src[id]; 

	switch ( ncac_req->status ) {

	    case NCAC_BUFFER_COMPLETE:  /* pending communication is done */
			
            NCAC_list_del_lock(&ncac_req->list, &NCAC_dev.req_list_lock);
			ret = NCAC_extent_done_access( ncac_req );
			
			break;

		case NCAC_COMPLETE:

            NCAC_list_del_lock(&ncac_req->list, &NCAC_dev.req_list_lock);
			break;

		default: /* error. leaking here. */
			fprintf(stderr, "NCAC_done_request: wrong status of internal request\n");
			ret = NCAC_REQ_STATUS_ERR;
			return ret;
	}


	/* prepare to return this request to the free list.
	 * We cannot just zero the ncac_req for all cases. 
	 * We want to reuse buffer inforation arrays to avoid
	 * allcations. */
	 
	if ( ncac_req->reserved_cbufcnt == 0 ) {
		id = ncac_req->id;
		memset( ncac_req, 0, sizeof(struct NCAC_req) );
		ncac_req->id = id;

	}else{ /* we want reuse buffer information arrays */
	    ncac_req->cbufcnt = 0;
	    ncac_req->mapping = 0;
		ncac_req->ioreq   = INVAL_IOREQ;
        ncac_req->read_out = 0;
	}
    ncac_req->status  = NCAC_REQ_UNUSED; 

    NCAC_list_add_tail_lock( &ncac_req->list, &NCAC_dev.free_req_list, &NCAC_dev.req_list_lock); 


	return ret;
}

static inline struct inode *get_inode(PVFS_fs_id coll_id, 
									PVFS_handle handle, PVFS_context_id context_id)
{
    struct inode *inode;

    if ( !inode_arr[handle] ) {
        inode=(struct inode*)malloc(sizeof(struct inode));

                /* initialize it */
                memset(inode, 0, sizeof(struct inode));

        inode->cache_stack = get_cache_stack();

	init_single_radix_tree(&inode->page_tree, NCAC_dev.get_value, NCAC_dev.max_b);

        inode_arr[handle] = inode;
		inode_arr[handle]->nrpages = 0;
		inode_arr[handle]->nr_dirty = 0;
		inode_arr[handle]->coll_id = coll_id;
		inode_arr[handle]->handle = handle;
		inode_arr[handle]->context_id = context_id;

		spin_lock_init(&inode->lock);

        INIT_LIST_HEAD(&(inode->clean_pages));
        INIT_LIST_HEAD(&(inode->dirty_pages));
    }
    return inode_arr[handle];
}

static inline void extent_dump(struct extent *extent)
{
	fprintf(stderr, "flags:%x\t status:%d\t	index:%d\t\n", (int)extent->flags, extent->status, (int)extent->index);
	fprintf(stderr, "writes:%d\t reads:%d\t	ioreq:%d\t\n", extent->writes, extent->reads, extent->ioreq);

}

static inline void list_dump(struct list_head *head)
{
    struct extent *page;
    struct list_head *tmp;

    if (!list_empty(head)) {
        tmp = head->next;
        while (tmp!=head) {
			page = list_entry(tmp, struct extent, lru);
			fprintf(stderr, "extent: %p\t", page); 
			extent_dump(page);

			tmp = tmp->next; 
	    } 
    }
}

void cache_dump_active_list(void)
{
   struct cache_stack *cache = get_cache_stack();

   fprintf(stderr, "active_list:\n");
   list_dump(&cache->active_list);
}
void cache_dump_inactive_list(void)
{
   struct cache_stack *cache = get_cache_stack();

   fprintf(stderr, "inactive_list:\n");
   list_dump(&cache->inactive_list);
}

static inline void req_list_dump(struct list_head *head)
{
    NCAC_req_t *req;
    struct list_head *tmp;

    if (!list_empty(head)) {
        tmp = head->next;
        while (tmp!=head) {
			req = list_entry(tmp, NCAC_req_t, list);
			fprintf(stderr, "req: %p\t", req); 
			//req_dump(req);

			tmp = tmp->next; 
	    } 
    }
    fprintf(stderr, "\n"); 
}

void cmp_list_dump(void)
{
   fprintf(stderr, "cmp list:	");
   req_list_dump(&NCAC_dev.comp_list);
}

void job_list_dump(void)
{
   fprintf(stderr, "job list:	");
   req_list_dump(&NCAC_dev.prepare_list);
}

static inline void list_dump_list(struct list_head *head)
{
    struct extent *page;
    struct list_head *tmp;

    if (!list_empty(head)) {
        tmp = head->next;
        while (tmp!=head) {
			page = list_entry(tmp, struct extent, list);
			fprintf(stderr, "extent: %p\t", page); 
			extent_dump(page);

			tmp = tmp->next; 
	    } 
	}	
}

void dirty_list_dump(int handle)
{
	struct inode *inode;

	inode = inode_arr[handle];

	list_dump_list(&inode->dirty_pages);
}
