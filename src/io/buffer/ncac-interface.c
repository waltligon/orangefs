#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "ncac-interface.h"
#include "internal.h"

/* cache_read_post()
 *
 * post a read request to NCAC. A read request could be a single
 * contiguous region, or a list of non-contiguous regions.   
 * 
 * Input:  desc:    read request descriptor
 *     user_ptr:    some pointer used by the caller.
 *
 * Output: request: request handle
 *         reply:   reply information.
 *
 * Return: 0: success, the request is submitted successfully. There is no
 *             any further progress on this request.
 *             request->status: NCAC_REQ_SUBMITTED
 *
 *         1: the request is submitted successfully. In addition, there is
 *            parital or complete progress on this request. In this case,
 *            there are two possible cases:
 *
 *                request->status: NCAC_REQ_BUF_PARTIAL
 *                request->status: NCAC_REQ_BUF_COMPLETE
 *            In either case, the "reply" parameter will return related
 *            buffer information. 
 *
 *         <0: error code 
 */
 
/* read request submit */
int cache_read_post(cache_read_desc_t *desc, 
                    cache_request_t *request,
                    cache_reply_t *reply,
                    void *user_ptr)
{
    int res;
    NCAC_req_t *ncac_req;
    
    /* no check here. We push this check to Trove layer */ 

    /* build an interanl request */
    ncac_req = NCAC_rwreq_build((NCAC_desc_t*)desc, NCAC_GEN_READ);
    if ( ncac_req == NULL ) {
        request->status = NCAC_REQ_BUILD_ERR;
        return -ENOMEM;
    }

    DPRINT("ncac_req=%p\n", ncac_req);

    /* ok. ready to submit a read request to NCAC */
    res = NCAC_rwjob_prepare(ncac_req, reply );
    if ( res < 0 ) {
        request->status = NCAC_SUBMIT_ERR;
        return res;
    }

    request->internal_id  = ncac_req->id;
    request->optype = ncac_req->optype;
    request->status = ncac_req->status;

    reply->count = 0;

    if ( ncac_req->error ) return ncac_req->error;

    if ( ncac_req->status == NCAC_PARTIAL_PROCESS || 
         ncac_req->status == NCAC_BUFFER_COMPLETE )
    {

        /* buffer information  has been filled */
        reply->cbuf_offset_array = ncac_req->cbufoff;
        reply->cbuf_size_array   = ncac_req->cbufsize;
        reply->cbuf_flag         = ncac_req->cbufflag;
        reply->count             = ncac_req->cbufcnt;

        /* TODO:
         * add colaesce buffers here to form bigger contiguous buffer
         * if possible 
         */

        return 1;
    }

    /* the read request is submitted successfully. No further 
     * progress on it yet.
     */

    return 0;
}


/* cache_write_post()
 *
 * post a write request to NCAC. A write request could be a single
 * contiguous region, or a list of non-contiguous regions.   
 * 
 * Input:  desc:    write request descriptor
 *     user_ptr:    some pointer used by the caller.
 *
 * Output: request: request handle
 *         reply:   reply information.
 *
 * Return: 0: success, the request is submitted successfully. There is no
 *             any further progress on this request.
 *             request->status: NCAC_REQ_SUBMITTED
 * 
 *         1: the request is submitted successfully. In addition, there is
 *            parital or complete progress on this request. In this case,
 *            there are three possible cases:
 *
 *                request->status: NCAC_REQ_BUF_PARTIAL
 *                request->status: NCAC_REQ_BUF_COMPLETE
 *            In either case, the "reply" parameter will return related
 *            buffer information. 
 *
 *                request->status: NCAC_REQ_COMPLETE
 *            The third case only happens when the caller supplies 
 *            its temporary buffer to the cache. If the cache has already
 *            copy all data from the temporary buffer to its buffers,
 *            this flag is set.  The cache then
 *            copy all  
 *
 *         <0: error code 
 */
 
/* write request submit */
int cache_write_post(cache_write_desc_t *desc,
                     cache_request_t *request,
                     cache_reply_t *reply,
                     void *user_ptr)
{
    struct NCAC_req *ncac_req;
    int res;

    /* no check here. We push this check to Trove layer */ 

    /* build an interanl request */
    ncac_req= NCAC_rwreq_build((NCAC_desc_t*)desc, NCAC_GEN_WRITE);
    if ( ncac_req == NULL ) {
        request->status = NCAC_REQ_BUILD_ERR;
        return -ENOMEM;
    }

    /* ok. ready to submit a read request to NCAC */
    res = NCAC_rwjob_prepare(ncac_req, reply );
    if ( res < 0 ) {
        request->status = NCAC_SUBMIT_ERR;
        return res;
    }

    request->internal_id  = ncac_req->id;
    request->optype = ncac_req->optype;
    request->status = ncac_req->status;

    reply->count = 0;

    if ( request->status == NCAC_PARTIAL_PROCESS || 
         request->status == NCAC_BUFFER_COMPLETE ){

        /* buffer information  has been filled */
        reply->cbuf_offset_array = ncac_req->cbufoff;
        reply->cbuf_size_array   = ncac_req->cbufsize;
        reply->cbuf_flag         = ncac_req->cbufflag;
        reply->count             = ncac_req->cbufcnt;

        return 1;
    }

    /* the read request is submitted successfully. No further
     * progress on it yet.
     */

    return 0;

}

/* sync request submit */
int cache_sync_post(cache_sync_desc_t *desc,
                    cache_request_t *request,
		    void *user_ptr)
{

     fprintf(stderr, "not implemented yet\n");
     return 0;
}

/* controls on the request handle */
/* test if a request is done */
int cache_req_test(cache_request_t *request, 
                   int *flag,
                   cache_reply_t *reply,
                   void *user_ptr)
{
    struct NCAC_req *ncac_req;
    int ret;

    reply->count = 0;

    ret = NCAC_check_request(request->internal_id, &ncac_req);

    if ( ret < 0 ) {
        return ret;
    }

    request->status = ncac_req->status;
    if ( request->status == NCAC_PARTIAL_PROCESS || 
         request->status == NCAC_BUFFER_COMPLETE ){

        /* buffer information  has been filled */
        reply->cbuf_offset_array = ncac_req->cbufoff;
        reply->cbuf_size_array   = ncac_req->cbufsize;
        reply->cbuf_flag         = ncac_req->cbufflag;
        reply->count             = ncac_req->cbufcnt;
    }

    if ( request->status == NCAC_BUFFER_COMPLETE || request->status == NCAC_COMPLETE )
		*flag = 1;
    else 
		*flag = 0;


    return 0;

}

int cache_req_testsome(int count, 
                       cache_request_t *request, 
                       int *outcount, int *indices, 
                       cache_reply_t *reply,
                       void *user_ptr)
{

     fprintf(stderr, "not implemented yet\n");
     return 0;

}


int cache_req_done(cache_request_t *request)
{
    int ret;

    ret = NCAC_done_request(request->internal_id);
    if ( ret < 0 ) {
        return ret;
    }
    request->status = NCAC_COMPLETE;

    return 0;
}
