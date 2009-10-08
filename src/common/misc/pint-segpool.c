#include <stdlib.h> /* sson */
#include "quicklist.h"
#include "pint-request.h"
#include "pint-segpool.h"
#include "gen-locks.h"
#include "id-generator.h"
#include <assert.h>
#include "server-config.h"

struct PINT_sp_segments
{
    PVFS_offset *offsets;
    PVFS_size *sizes;
    int count;
    struct qlist_head link;
};

struct PINT_segpool_handle
{
    PINT_Request_state *file_req_state;
    PINT_Request_state *mem_req_state;
    PINT_request_file_data filedata;
    enum PINT_segpool_type type;
    struct qlist_head segments_list;
    gen_mutex_t mutex;
};

int PINT_segpool_init(
    PVFS_Request mem_request,
    PVFS_Request file_request,
    PVFS_size file_size,
    PVFS_offset request_offset,
    PVFS_size aggregate_size,
    uint32_t server_number,
    uint32_t server_count,
    PINT_dist *dist,
    enum PINT_segpool_type type,
    PINT_segpool_handle_t *h)
{
    struct PINT_segpool_handle *handle;

    handle = malloc(sizeof(*handle));
    if(!handle)
    {
        return -PVFS_ENOMEM;
    }

    handle->file_req_state = PINT_new_request_state(file_request);
    PINT_REQUEST_STATE_SET_TARGET(handle->file_req_state, request_offset);
    if(aggregate_size > -1)
    {
        PINT_REQUEST_STATE_SET_FINAL(handle->file_req_state, 
				     aggregate_size+request_offset);
    }
    else
    {
        PINT_REQUEST_STATE_SET_FINAL(handle->file_req_state,
                                     request_offset +
                                     PINT_REQUEST_TOTAL_BYTES(mem_request));
    }

    if(mem_request)
	handle->mem_req_state = PINT_new_request_state(mem_request);
    else
	handle->mem_req_state = NULL;
    
    handle->filedata.fsize = file_size;
    handle->filedata.dist = dist;
    handle->filedata.server_nr = server_number;
    handle->filedata.server_ct = server_count;
    handle->type = type;

    if(type == PINT_SP_SERVER_WRITE || type == PINT_SP_CLIENT_WRITE)
    {
        handle->filedata.extend_flag = 1;
    }
    else
    {
        handle->filedata.extend_flag = 0;
    }

    INIT_QLIST_HEAD(&handle->segments_list);

    gen_mutex_init(&handle->mutex);

    *h = handle;
    return 0;
}

int PINT_segpool_destroy(PINT_segpool_handle_t h)
{
    struct PINT_sp_segments *segments;
    struct PINT_sp_segments *tmp;

    gen_mutex_lock(&h->mutex);
    PINT_free_request_states(h->file_req_state);
    PINT_free_request_states(h->mem_req_state);

    qlist_for_each_entry_safe(segments, tmp, &h->segments_list, link)
    {
        if(segments->count > 0)
        {
            free(segments->offsets);
            free(segments->sizes);
        }
        qlist_del(&segments->link);
        free(segments);
    }

    gen_mutex_unlock(&h->mutex);
    gen_mutex_destroy(&h->mutex);
    free(h);

    return 0;
}

int PINT_segpool_register(PINT_segpool_handle_t h,
                          PINT_segpool_unit_id *id)
{
    struct PINT_sp_segments *segments;

    segments = malloc(sizeof(*segments));
    if(!segments)
    {
        return -PVFS_ENOMEM;
    }

    segments->offsets = NULL;
    segments->sizes = NULL;
    segments->count = 0;
    qlist_add(&segments->link, &h->segments_list);
    id_gen_fast_register((PVFS_id_gen_t *)id, segments);
    return 0;
}

/**
 * Takes the handle and requester id as inputs.  The bytes
 * field is an inout parameter.  As input, it should dereference
 * to the max bytes that can be summed for the returned segments,
 * and as output, it dereferences to the total bytes of all the
 * segments returned.
 * The count parameter is an out parameter, dereferencing to the
 * number of segments returned.
 * The offsets and sizes are output arrays * the size of *count.
 *
 * The offsets and sizes do not need to be freed after each call
 * to PINT_segpool_take_segments.
 */
int PINT_segpool_take_segments(PINT_segpool_handle_t h,
                               PINT_segpool_unit_id id,
                               PVFS_size *bytes,
                               int *count,
                               PVFS_offset **offsets,
                               PVFS_size **sizes)
{
    struct PINT_sp_segments *segments;
    PINT_Request_result result;
    PINT_Request_result result_tmp;
    int type;
    int ret;

    segments = id_gen_fast_lookup(id);

    result.bytemax = *bytes;
    result.segs = 0;
    result.bytes = 0;

    gen_mutex_lock(&h->mutex);

    if(PINT_REQUEST_DONE(h->file_req_state))
    {
        *bytes = 0;
        *count = 0;
        *offsets = NULL;
        *sizes = NULL;
        gen_mutex_unlock(&h->mutex);
        return 0;
    }
    
    //gossip_debug(GOSSIP_IO_DEBUG, "%s: 1: count=%d\n", __func__, segments->count);
    if(segments->count == 0)
    {
        /* first time we need to allocate offset and size arrays */
        result.segmax = 1;
        result.offset_array = NULL;
        result.size_array = NULL;
        ret = PINT_process_request(h->file_req_state,
                                   h->mem_req_state,
                                   &h->filedata,
                                   &result,
                                   PINT_CKSIZE);
	//gossip_debug(GOSSIP_IO_DEBUG, "%s: 1-0: ret=%d, result.bytes=%d, result.segs=%d\n", __func__, ret, result.bytes, result.segs);
        if(ret != 0)
        {
            goto done;
        }

        if(result.segs == 0)
        {
            *bytes = 0;
            *count = 0;
            *offsets = NULL;
            *sizes = NULL;
            goto done;
        }

        segments->count = result.segs;
        segments->offsets = realloc(
            segments->offsets, sizeof(PVFS_offset)*result.segs);
        if(!segments->offsets)
        {
            gen_mutex_unlock(&h->mutex);
            return -PVFS_ENOMEM;
        }
        segments->sizes = realloc(
            segments->sizes, sizeof(PVFS_size)*result.segs);
        if(!segments->sizes)
        {
            free(segments->offsets);
            gen_mutex_unlock(&h->mutex);
            return -PVFS_ENOMEM;
        }

        result.segs = 0;
        result.bytes = 0;
    }

    result.segmax = segments->count;
    result.offset_array = segments->offsets;
    result.size_array = segments->sizes;

    if(h->type == PINT_SP_SERVER_WRITE || h->type == PINT_SP_SERVER_READ)
    {
        type = PINT_SERVER;
    }
    else
    {
        type = PINT_CLIENT;
    }

    ret = PINT_process_request(h->file_req_state,
                               h->mem_req_state,
                               &h->filedata,
                               &result,
                               type);

    //gossip_debug(GOSSIP_IO_DEBUG, "%s: 2: ret=%d, result.bytes=%d, result.bytemax=%d\n", __func__, ret, result.bytes, result.bytemax);
    //gossip_debug(GOSSIP_IO_DEBUG, "%s: 2: result.segmax=%d\n", __func__, result.segmax);

    if(ret != 0)
    {
        goto done;
    }

    /* what if there were more segments this time than last time?
     * realloc the offset and sizes arrays if so
     */
    if(!PINT_REQUEST_DONE(h->file_req_state) && result.bytes < result.bytemax)
    {
        int prev_segment_count = result.segs;
        assert(result.segmax == result.segs);

        result_tmp.bytemax = result.bytemax - result.bytes;
        result_tmp.segmax = 1;
        result_tmp.segs = 0;
        result_tmp.bytes = 0;
        ret = PINT_process_request(h->file_req_state,
                                   h->mem_req_state,
                                   &h->filedata,
                                   &result_tmp,
                                   PINT_CKSIZE);
#if 0
	gossip_debug(GOSSIP_IO_DEBUG, "%s: 2-1: ret=%d, result_tmp.bytes=%d, result_tmp.bytemax=%d\n", __func__, ret, result_tmp.bytes, result_tmp.bytemax);
	gossip_debug(GOSSIP_IO_DEBUG, "%s: 2-1: ret=%d, result_tmp.segs=%d\n",
		 __func__, ret, result_tmp.segs);
	gossip_debug(GOSSIP_IO_DEBUG, "%s: 2-1: ret=%d, result_tmp.segmax=%d\n", __func__, ret, result_tmp.segmax);
	gossip_debug(GOSSIP_IO_DEBUG, "%s: 2-1: ret=%d, result.bytemax=%d\n", __func__, ret, result.bytemax);
#endif
	if(ret != 0)
        {
            goto done;
        }

    	/* FIXME: nothing to process, so copy prior values and return */
	if(result_tmp.bytes == 0) {
	    *bytes = result.bytes;
	    *count = result.segs;
	    *offsets = result.offset_array;
	    *sizes = result.size_array;
	    goto done;
	}

	/* At this point, we know that there's something to process.
	   so we use result_tmp */
	result = result_tmp; 

        segments->count += result.segs;
#if 0
	gossip_debug(GOSSIP_IO_DEBUG, "%s: segments->count=%d\n", 
		     __func__, segments->count);
	gossip_debug(GOSSIP_IO_DEBUG, "%s: result.segmax=%d\n", 
		     __func__, result.segmax);
#endif
	segments->offsets = realloc(segments->offsets, 
				    sizeof(PVFS_offset)*segments->count);
	segments->sizes = realloc(segments->sizes, 
				      sizeof(PVFS_size)*segments->count);
	result.offset_array = segments->offsets + prev_segment_count;
        result.size_array = segments->sizes + prev_segment_count;
        result.segmax = result.segs;
	
        result.segs = 0;
        result.bytes = 0;

        ret = PINT_process_request(h->file_req_state,
                                   h->mem_req_state,
                                   &h->filedata,
                                   &result,
                                   type);
        if(ret != 0)
        {
            goto done;
        }

    }

    *bytes = result.bytes;
    *count = result.segs;
    *offsets = result.offset_array;
    *sizes = result.size_array;

 done:
    gen_mutex_unlock(&h->mutex);
    return ret;
}

#include "pint-dist-utils.h"
#if TEST
/**
 * Testing code for the above.  This tests:
 *
 */
int main(int argc, char *argv[])
{
    PINT_segpool_handle_t handle;
    PINT_segpool_unit_id id[3];
    PVFS_size bytes[3];
    int count[3];
    PVFS_offset *offsets[3];
    PVFS_size *sizes[3];

    PVFS_Request mem_req, file_req;

    PINT_dist_initialize(NULL);

    PINT_dist *dist = PINT_dist_create("simple_stripe");
    PINT_dist_lookup(dist);

    PVFS_Request_vector(1000, 1024, 6, PVFS_DOUBLE, &mem_req);
    PVFS_Request_vector(2000, 512, 6, PVFS_DOUBLE, &file_req);

    PINT_segpool_init(NULL,/*mem_req,*/
                      file_req,
                      1024*1024*8,
                      0,
                      1024*8,
                      1,
                      4,
                      dist,
                      PINT_SP_SERVER_READ,
                      &handle);

    PINT_segpool_register(handle, &id[0]);
    PINT_segpool_register(handle, &id[1]);
    PINT_segpool_register(handle, &id[2]);

    bytes[0] = 1024*6;
    bytes[1] = 1024*6;
    bytes[2] = 1024*6;

    while(1)
    {
        PINT_segpool_take_segments(handle, id[0], &(bytes[0]), &(count[0]),
                                   &(offsets[0]), &(sizes[0]));
	printf("count[0]=%d\n", count[0]);
	printf("bytes[0]=%d\n", bytes[0]);
        if(count[0] == 0) break;


        PINT_segpool_take_segments(handle, id[1], &(bytes[1]), &(count[1]),
                                   &(offsets[1]), &(sizes[1]));
        if(count[1] == 0) break;

        PINT_segpool_take_segments(handle, id[2], &(bytes[2]), &(count[2]),
                                   &(offsets[2]), &(sizes[2]));
        if(count[2] == 0) break;
    }

    return 0;
}
#endif
/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=4 sts=4 sw=4 expandtab
 */
