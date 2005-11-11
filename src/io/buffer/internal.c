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
#include "pvfs2-internal.h"

extern struct NCAC_dev  NCAC_dev;
void cache_dump_active_list(void);
void cache_dump_inactive_list(void);
static void NCAC_list_add_tail_lock(struct list_head *new, struct list_head *head, NCAC_lock *lock);
static void NCAC_list_del_lock(struct list_head *entry, NCAC_lock *lock);
static void NCAC_read_request_from_list_lock(struct list_head *head, NCAC_lock *lock, NCAC_req_t ** ncac_req_ptr);

/* This file contains NCAC internal functions. */

static inline struct inode *get_inode( PVFS_fs_id, PVFS_handle , PVFS_context_id);
static inline int NCAC_rwjob_prepare_single(NCAC_req_t *ncac_req);
static inline int NCAC_rwjob_prepare_list(NCAC_req_t *ncac_req);

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
	if ( !new ) {
		fprintf(stderr, "there is no entry in the free req list\n");
		return NULL;
	}
	list_del_init(new);

	list_unlock(&NCAC_dev.req_list_lock); 

	req = list_entry(new->prev, NCAC_req_t, list);

	return req;  
}


/* add a request into the tail of a list exclusively. */
static void NCAC_list_add_tail_lock(struct list_head *new, struct list_head *head, NCAC_lock *lock)
{
    list_lock(lock);

    list_add_tail(new, head);

    list_unlock(lock);

}

/* delete an entry from its list */
static void NCAC_list_del_lock(struct list_head *entry, NCAC_lock *lock)
{
    list_lock(lock);

    list_del(entry);

    list_unlock(lock);
}

/* read an entry without mark from a list and mark the entry. */
static void NCAC_read_request_from_list_lock(struct list_head *head, NCAC_lock *lock, NCAC_req_t ** ncac_req_ptr)
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
	int tmp_off, tmp_size;

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

    if ( desc->buffer ) { /* copy data into the user's buffer */
        if ( optype == NCAC_GEN_READ ) 
            ncac_req->optype = NCAC_BUF_READ;
        else ncac_req->optype = NCAC_BUF_WRITE;

        ncac_req->usrbuf  = desc->buffer;
        ncac_req->usrlen  = desc->len;
    }else{ /* use cache buffers for communication */
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
		tmp_off = desc->stream_array_count*sizeof(PVFS_offset);
		tmp_size = desc->stream_array_count*sizeof(PVFS_size);

        iovec = (void*) malloc( tmp_off + tmp_size );
        if (iovec == NULL ) {
            ncac_req->status = NCAC_NO_MEM;
            return ncac_req;
        }

        ncac_req->offcnt = desc->stream_array_count;
        ncac_req->offvec  = (PVFS_offset*)iovec;
        ncac_req->sizevec = (PVFS_size *)( (unsigned long)iovec + tmp_off );

        /* copy the user request information into an internal request */
		memcpy(ncac_req->offvec, desc->stream_offset_array, tmp_off);
		memcpy(ncac_req->sizevec, desc->stream_size_array, tmp_size);
    }

    /* success */
    ncac_req->status = NCAC_OK;
    return ncac_req;
}

/*
 * NCAC_rwjob_prepare(): does three things:
 * (1) allocate resource; caculate index, offset, and length; 
 * (2) put the request in the internal job list
 * (3) make progress of the requests in the job list.
 */
int NCAC_rwjob_prepare(NCAC_req_t *ncac_req, NCAC_reply_t *reply )
{
	int ret;

    /* prepare the request */
	if ( !ncac_req->offcnt ) { /* only one contiguous segment */

        ret = NCAC_rwjob_prepare_single(ncac_req);

    }else{      /* multiple segements */

        ret = NCAC_rwjob_prepare_list(ncac_req);

    }
    if ( ret < 0 ){
        ncac_req->error = ret;
        return ret;
    }

	/* put the request in the internal job list: thread safe */

	NCAC_list_add_tail_lock(&ncac_req->list, &NCAC_dev.prepare_list, 
                            &NCAC_dev.req_list_lock);

	ncac_req->status = NCAC_REQ_SUBMITTED;

	DPRINT("NCAC_rwjob_prepare: %p submitted\n", ncac_req);

	/* make progress of jobs: thread safe.
	 * Choices here are: 1) do one job; 2) scan the whole list. 
	 * Choose 1) here. 
     */
	//ret = NCAC_do_jobs(&(NCAC_dev.req_list), &(NCAC_dev.bufcomp_list), &(NCAC_dev.comp_list), &NCAC_dev.req_list_lock); 

	ret = NCAC_do_a_job(ncac_req, &(NCAC_dev.prepare_list), 
                    &(NCAC_dev.bufcomp_list), 
                    &(NCAC_dev.comp_list), 
                    &NCAC_dev.req_list_lock);

	if ( ret < 0 ) {
		ncac_req->error = NCAC_JOB_DO_ERR;
		ncac_req->status = NCAC_ERR_STATUS;
		return ret;
	}

	ncac_req->error  = NCAC_OK;

	return 0;
}


/* NCAC_rwjob_prepare_single: Given a request which accesses only one
 *      file region, we prepare needed resources for this request:
 *  1) extent cache buffers;
 *  2) Communication buffer address;    
 *  3) Communication buffer sizes;
 *  4) Communication buffer flags;
 *  Given the extent size is 32768 bytes, if a request wants to
 *  read data 32768 bytes from 1024,
 *      (1) two extents: 0-32765, and 32768-65535
 *      (2) comm bufers: extent1.addr+1024, extent2.addr
 *      (3) comm bufer size: 31744, 1024
 *      (4) if data is ready, flag is set.
 *  In this case, the number of extents and the number of communication
 *  buffers are same.
 */

static inline int NCAC_rwjob_prepare_single(NCAC_req_t *ncac_req)
{
	int extcnt;  /* cache extent count */
    int comcnt;  /* communication buffer count */
    int allocsize;

    PVFS_offset   *foff;
	char          **cbufoff;
	PVFS_size     *cbufsize;
    int           *cbufflag;
    unsigned long firstoff;
    
    int i;

    extcnt = (ncac_req->pos + ncac_req->size + NCAC_dev.extsize -1) /
                NCAC_dev.extsize - ncac_req->pos/NCAC_dev.extsize; 
    comcnt = extcnt;

	if ( ncac_req->reserved_cbufcnt < comcnt ) {
		if ( ncac_req->cbufoff ) free( ncac_req->cbufoff);

		allocsize = ( sizeof(PVFS_offset) + sizeof(char*) + sizeof(PVFS_size)
            + sizeof(struct extent *) + 3*sizeof(int) ) * comcnt; 

		ncac_req->foff  =(PVFS_offset*) malloc(allocsize); 

		if ( ncac_req->foff == NULL ) {
			ncac_req->error = -ENOMEM;
			return -ENOMEM;
		}

		ncac_req->cbufoff  =(char**) & ncac_req->foff[comcnt];
		ncac_req->cbufsize =(PVFS_size*)  &ncac_req->cbufoff[comcnt];
		ncac_req->cbufhash =(struct extent**)
                            &ncac_req->cbufsize[comcnt];
		ncac_req->cbufflag =(int*) &ncac_req->cbufhash[comcnt];
		ncac_req->cbufrcnt =(int*) &ncac_req->cbufflag[comcnt];
		ncac_req->cbufwcnt =(int*) &ncac_req->cbufrcnt[comcnt];

		ncac_req->reserved_cbufcnt = comcnt;

	    memset(ncac_req->foff, 0, allocsize);
    }

	ncac_req->cbufcnt = comcnt;

    foff = ncac_req->foff;
    cbufoff = ncac_req->cbufoff;
    cbufsize = ncac_req->cbufsize;
    cbufflag = ncac_req->cbufflag;

    /* Setup the related values for foff, cbufoff, and cbufsize */

    firstoff = (unsigned long) (ncac_req->pos & (NCAC_dev.extsize -1)); 
    foff[0] = ncac_req->pos - firstoff;
    cbufoff[0] = (char*)firstoff;     /* offsize to the extent address */
    cbufsize[0] = NCAC_dev.extsize - firstoff;
    cbufflag[0] = NCAC_COMM_NOT_READY;

    for ( i= 1; i < comcnt; i++){
        foff[i] = foff[i-1] + NCAC_dev.extsize;
        cbufoff[i] = 0;
        cbufsize[i] = NCAC_dev.extsize;
        cbufflag[i] = NCAC_COMM_NOT_READY;
    }
    /* adjust the size of the last buffer in each segment. */
    cbufsize[comcnt-1] = (ncac_req->pos + ncac_req->size)% NCAC_dev.extsize;

#if 1
    fprintf(stderr, "[%s] exit %d comm buffers\n", __FUNCTION__, comcnt);
    for (i=0; i<comcnt; i++){
        fprintf(stderr, "fpos:%lld, buf_off:%ld, size:%lld\n", lld(foff[i]),
(unsigned long)cbufoff[i], lld(cbufsize[i]));
    }
#endif

    fprintf(stderr, "[%s] exit %d comm buffers\n", __FUNCTION__, comcnt);
    return 0;
}

/* NCAC_rwjob_prepare_list: Given a request which accesses a list of
 *      fire regions, we prepare needed resources for this request:
 *  1) extent cache buffers;
 *  2) Communication buffer address;    
 *  3) Communication buffer sizes;
 *  4) Communication buffer flags;
 *  Given the extent size is 32768 bytes, if a request wants to
 *  read data the following regions: (1024, 32768) and (65530, 32768)
 *      (1) Three extents: 0-32765, 32768-65535, 65536-98303
 *      (2) Communication buffers:
 *            extent1.addr+1024, extent2.addr,
 *            extent2.addr+32762, extent3.addr
 *      (3) Communication buffer size:
 *                31744, 1024, 6, and 32762
 *  This example shows that:
 *    (A) For the underlying I/O system, we are going to read
 *        three extents;
 *    (B) For the upper communcation system, we are goint to
 *        user four different buffers.
 *   The number of communication buffers is equal to or larger 
 *   than the number of needed extents.
 */ 

struct freg_tuple
{
    PVFS_offset fpos;
    PVFS_size   size;
};

static int comp_pos(const PVFS_offset *num1, const PVFS_offset *num2)
{
    if (*num1 <  *num2) return -1;
    if (*num1 == *num2) return  0;
    if (*num1 >  *num2) return  1;
    return 0;
}

static inline int NCAC_rwjob_prepare_list(NCAC_req_t *ncac_req)
{
	int extcnt;  /* cache extent count */
    int comcnt;  /* communication buffer count */
    int allocsize;

    PVFS_offset   *foff;
	char          **cbufoff;
	PVFS_size     *cbufsize;
    int           *cbufflag;
    unsigned long   firstoff;

    int i, j;
    int cnt;

    struct freg_tuple *fregions;
    
    fregions = (struct freg_tuple *)malloc(ncac_req->offcnt *
                    sizeof(struct freg_tuple));
    if ( NULL == fregions){
		ncac_req->error = -ENOMEM;
        return -ENOMEM;
    }

    extcnt = 0;
    for (i = 0; i < ncac_req->offcnt; i ++) {
        extcnt += (ncac_req->offvec[i] + ncac_req->sizevec[i] +
                NCAC_dev.extsize -1)/NCAC_dev.extsize - 
                ncac_req->offvec[i]/NCAC_dev.extsize;

        fregions[i].fpos = ncac_req->offvec[i];
        fregions[i].size = ncac_req->sizevec[i];
    }

    /* Some extents counted by "extcnt" may be same. Also the
     * number of communication buffers should be same as the 
     * extcnt. Use "comcnt" to overprovision resources.
     */

    comcnt = extcnt;

	if ( ncac_req->reserved_cbufcnt < comcnt ) {
		if ( ncac_req->cbufoff ) free( ncac_req->cbufoff);

		allocsize = ( sizeof(PVFS_offset) + sizeof(char*) + sizeof(PVFS_size)
            + sizeof(struct extent *) + 3*sizeof(int) ) * comcnt; 

		ncac_req->foff  =(PVFS_offset*) malloc(allocsize); 

		if ( ncac_req->foff == NULL ) {
			ncac_req->error = -ENOMEM;

            free(fregions);

			return -ENOMEM;
		}

		ncac_req->cbufoff  =(char**) & ncac_req->foff[comcnt];
		ncac_req->cbufsize =(PVFS_size*)  &ncac_req->cbufoff[comcnt];
		ncac_req->cbufhash =(struct extent**)
                            &ncac_req->cbufsize[comcnt];
		ncac_req->cbufflag =(int*) &ncac_req->cbufhash[comcnt];
		ncac_req->cbufrcnt =(int*) &ncac_req->cbufflag[comcnt];
		ncac_req->cbufwcnt =(int*) &ncac_req->cbufrcnt[comcnt];

		ncac_req->reserved_cbufcnt = comcnt;

	    memset(ncac_req->foff, 0, allocsize);
    }
    
    foff = ncac_req->foff;
    cbufoff = ncac_req->cbufoff;
    cbufsize = ncac_req->cbufsize;
    cbufflag = ncac_req->cbufflag;

    /* How many different extents are needed? Put them in an
     * ordered manner to be friendly to the underlying I/O system.
     * What are communication buffers used for the upper layer?
     * (offset to the related extent, size).
     */
    /* quick sort the list of file regions. If the upper layer
     * can present the file regions in an ordered manner, we can
     * eliminate this sorting.
     */
    qsort(fregions, ncac_req->offcnt, sizeof(struct freg_tuple), (void*)comp_pos);

#if  1
    for (i=0; i<ncac_req->offcnt; i++){
        fprintf(stderr, "fpos:%lld, size:%lld\n", lld(fregions[i].fpos),
lld(fregions[i].size));
    }
#endif

    comcnt = 0;
    for ( i =0; i <ncac_req->offcnt; i++){
        cnt = (fregions[i].fpos+fregions[i].size+NCAC_dev.extsize-1)/
            NCAC_dev.extsize - fregions[i].fpos/NCAC_dev.extsize;

        firstoff=(unsigned long)(fregions[i].fpos & (NCAC_dev.extsize -1)); 

        foff[comcnt] = fregions[i].fpos - firstoff;
        cbufoff[comcnt] = (char*)firstoff;
        cbufsize[comcnt] = NCAC_dev.extsize - firstoff;
        cbufflag[comcnt] = NCAC_COMM_NOT_READY;

        for ( j= 1; j < cnt; j++){
            foff[comcnt+j] = foff[comcnt+j-1] + NCAC_dev.extsize;
            cbufoff[comcnt+j] = 0;
            cbufsize[comcnt+j] = NCAC_dev.extsize;
            cbufflag[comcnt+j] = NCAC_COMM_NOT_READY;
        }
        /* adjust the size of the last buffer in each segment. */
        cbufsize[comcnt+cnt-1] -= (fregions[i].fpos+fregions[i].size) % 
                    NCAC_dev.extsize;

        comcnt += cnt;
    }

    /* so far, in the ncac_req.foff, some extents are probably same,
     * but they are consecutive.
     */

    free(fregions);

    ncac_req->cbufcnt = comcnt;

#if 1
    fprintf(stderr, "[%s] exit %d comm buffers\n", __FUNCTION__, comcnt);
    for (i=0; i<comcnt; i++){
        fprintf(stderr, "fpos:%lld, buf_off:%ld, size:%lld\n", lld(foff[i]),
(unsigned long)cbufoff[i], lld(cbufsize[i]));
    }
#endif

    return 0;
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

int NCAC_do_jobs(struct list_head *prep_list, struct list_head *bufcomp_list,
				 struct list_head *comp_list, NCAC_lock *lock)
{
	int ret; 
	NCAC_req_t *ncac_req;

dojob:

	/* read a request from the prep_list job. When a job is read out 
	 * (NOT taken from the list), there is a flag to indicate that 
	 * someone else has read this request out. So get_request_from_list 
	 * always returns a request which is not read out by others 
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

int NCAC_do_a_job(NCAC_req_t *ncac_req, struct list_head *prep_list, 
				struct list_head *bufcomp_list, 
                struct list_head *comp_list, NCAC_lock *lock)
{
	int ret;

    fprintf(stderr, "NCAC_do_a_job enter\n");

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

    fprintf(stderr, "NCAC_do_a_job exit\n");
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

static inline struct inode *search_inode_list (PVFS_handle handle)
{
	int inode_index;
	struct inode * cur;

	inode_index = handle % MAX_INODE_NUM;

	cur = inode_arr[inode_index]; 
	while ( NULL != cur ) {
		if ( cur->handle == handle ) return cur;	
		cur = cur->next;
	}	

	return NULL;
}

/* get_inode: give a fs_id and a file handler, an inode-like structure
 *            is allocated. Since handle is an arbitrary number, we should
 *            have a mapping between this handler and the index of inode.
 * get_inode should be called under some lock because two callers may
 *     work on the same collision list.  
 */
static inline struct inode *get_inode(PVFS_fs_id coll_id, 
				PVFS_handle handle, PVFS_context_id context_id)
{
	struct inode *inode;
	int inode_index;

	inode_index = handle % MAX_INODE_NUM;

	/* search the inode list with the index of "inode_index" */
	inode = search_inode_list (handle);

	fprintf(stderr, "handle: %lld, index: %d, inode:%p\n", lld(handle), inode_index, inode);

	if ( NULL == inode ){
		inode=(struct inode*)malloc(sizeof(struct inode));

		/* initialize it */
		memset(inode, 0, sizeof(struct inode));

		inode->cache_stack = get_cache_stack();
		inode->nrpages = 0;
		inode->nr_dirty = 0;
		inode->coll_id = coll_id;
		inode->handle = handle;
		inode->context_id = context_id;

		init_single_radix_tree(&inode->page_tree, NCAC_dev.get_value, NCAC_dev.max_b);

		spin_lock_init(&inode->lock);

		INIT_LIST_HEAD(&(inode->clean_pages));
		INIT_LIST_HEAD(&(inode->dirty_pages));

		/* put the new inode to the head of the collision list */
		inode->next = inode_arr[inode_index];
		inode_arr[inode_index] = inode;
	}

	return inode;
}



static inline void extent_dump(struct extent *extent)
{
	fprintf(stderr, "flags:%x\t status:%d\t	index:%d\t\n", (int)extent->flags, extent->status, (int)extent->index);
	fprintf(stderr, "writes:%d\t reads:%d\t	ioreq:%lld\t\n", extent->writes, extent->reads, lld(extent->ioreq));

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

static void cmp_list_dump(void) __attribute__((unused));
static void cmp_list_dump(void)
{
   fprintf(stderr, "cmp list:	");
   req_list_dump(&NCAC_dev.comp_list);
}

static void job_list_dump(void) __attribute__((unused));
static void job_list_dump(void)
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

static void dirty_list_dump(int handle) __attribute__((unused));
static void dirty_list_dump(int handle)
{
	struct inode *inode;

	inode = inode_arr[handle];

	list_dump_list(&inode->dirty_pages);
}
