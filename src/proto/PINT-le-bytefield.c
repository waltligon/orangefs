/*
 * (C) 2001 Clemson University and The University of Chicago
 * (C) 2003 Pete Wyckoff, Ohio Supercomputer Center <pw@osc.edu>
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C  /* trigger actual definitions */
#include "endecode-funcs.h"
#include "bmi.h"
#include "bmi-byteswap.h"
#include "gossip.h"
#include "pvfs2-dist-basic.h"
#include "pvfs2-types.h"
#include "pvfs2-req-proto.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"
#include "src/io/description/pint-request.h"  /* for PINT_Request */
#include "src/io/description/pint-distribution.h"  /* for PINT_dist_lookup */

/* defined later */
PINT_encoding_table_values le_bytefield_table;
static int check_req_size(struct PVFS_server_req *req);
static int check_resp_size(struct PVFS_server_resp *resp);

static int initializing_sizes = 0;

/* an array of structs for storing precalculated maximum encoding sizes
 * for each type of server operation 
 */
static struct {
    int req;
    int resp;
} max_size_array[PVFS_MAX_SERVER_OP+1];

/* lebf_initialize()
 *
 * initializes the encoder module, calculates max sizes of each request type 
 * in advance
 *
 * no return value
 */
static void lebf_initialize(void)
{
    struct PVFS_server_req req;
    struct PVFS_server_resp resp;
    int i;
    int reqsize, respsize;
    int noreq;
    PINT_dist tmp_dist;
    PINT_Request tmp_req;
    char *tmp_name = strdup("foo");
    const int init_big_size = 1024 * 1024;

    /*
     * Some messages have extra structures, and even indeterminate sizes
     * which are hand-calculated here.  Also some fields must be initialized
     * for encoding to work properly.
     */
    memset(&tmp_dist, 0, sizeof(tmp_dist));
    tmp_dist.dist_name = strdup(PVFS_DIST_BASIC_NAME);
    if (PINT_dist_lookup(&tmp_dist)) {
	gossip_err("%s: dist %s does not exist?!?\n",
	  __func__, tmp_dist.dist_name);
	exit(1);
    }
    memset(&tmp_req, 0, sizeof(tmp_req));

    initializing_sizes = 1;
    for (i=0; i<=PVFS_MAX_SERVER_OP; i++) {
	req.op = resp.op = i;
	reqsize = 0;
	respsize = 0;
	noreq = 0;
	switch (i) {
	    case PVFS_SERV_INVALID:
	    case PVFS_SERV_PERF_UPDATE:
	    case PVFS_SERV_JOB_TIMER:
		/* never used, skip initialization */
		continue;
	    case PVFS_SERV_GETCONFIG:
		resp.u.getconfig.fs_config_buf = tmp_name;
		resp.u.getconfig.server_config_buf = tmp_name;
		respsize = extra_size_PVFS_servresp_getconfig;
		break;
	    case PVFS_SERV_LOOKUP_PATH:
		req.u.lookup_path.path = "";
		resp.u.lookup_path.handle_count = 0;
		resp.u.lookup_path.attr_count = 0;
		reqsize = extra_size_PVFS_servreq_lookup_path;
		respsize = extra_size_PVFS_servresp_lookup_path;
		break;
	    case PVFS_SERV_CREATE:
		/* can request a range of handles */
		req.u.create.handle_extent_array.extent_count = 0;
		reqsize = extra_size_PVFS_servreq_create;
		break;
	    case PVFS_SERV_REMOVE:
		/* nothing special, let normal encoding work */
		break;
	    case PVFS_SERV_MGMT_REMOVE_OBJECT:
		/* nothing special, let normal encoding work */
		break;
	    case PVFS_SERV_MGMT_REMOVE_DIRENT:
		req.u.mgmt_remove_dirent.entry = tmp_name;
		reqsize = extra_size_PVFS_servreq_mgmt_remove_dirent;
		break;
	    case PVFS_SERV_IO:
		req.u.io.io_dist = &tmp_dist;
		req.u.io.file_req = &tmp_req;
		reqsize = extra_size_PVFS_servreq_io;
		break;
	    case PVFS_SERV_GETATTR:
		resp.u.getattr.attr.mask = 0;
		respsize = extra_size_PVFS_servresp_getattr;
		break;
	    case PVFS_SERV_SETATTR:
		req.u.setattr.attr.mask = 0;
		reqsize = extra_size_PVFS_servreq_setattr;
		break;
	    case PVFS_SERV_CRDIRENT:
		req.u.crdirent.name = tmp_name;
		reqsize = extra_size_PVFS_servreq_crdirent;
		break;
	    case PVFS_SERV_RMDIRENT:
		req.u.rmdirent.entry = tmp_name;
		reqsize = extra_size_PVFS_servreq_rmdirent;
		break;
	    case PVFS_SERV_CHDIRENT:
		req.u.chdirent.entry = tmp_name;
		reqsize = extra_size_PVFS_servreq_chdirent;
		break;
	    case PVFS_SERV_TRUNCATE:
		/* nothing special */
		break;
	    case PVFS_SERV_MKDIR:
		req.u.mkdir.handle_extent_array.extent_count = 0;
		req.u.mkdir.attr.mask = 0;
		reqsize = extra_size_PVFS_servreq_mkdir;
		break;
	    case PVFS_SERV_READDIR:
		resp.u.readdir.dirent_count = 0;
		respsize = extra_size_PVFS_servresp_readdir;
		break;
	    case PVFS_SERV_FLUSH:
		/* nothing special */
		break;
	    case PVFS_SERV_MGMT_SETPARAM:
		/* nothing special */
		break;
	    case PVFS_SERV_MGMT_NOOP:
		/* nothing special */
		break;
	    case PVFS_SERV_STATFS:
		/* nothing special */
		break;
	    case PVFS_SERV_WRITE_COMPLETION:
		/* only a response, but nothing special there */
		noreq = 1;
		break;
	    case PVFS_SERV_MGMT_PERF_MON:
		resp.u.mgmt_perf_mon.perf_array_count = 0;
		respsize = extra_size_PVFS_servresp_mgmt_perf_mon;
		break;
	    case PVFS_SERV_MGMT_ITERATE_HANDLES:
		resp.u.mgmt_iterate_handles.handle_count = 0;
		respsize = extra_size_PVFS_servresp_mgmt_iterate_handles;
		break;
	    case PVFS_SERV_MGMT_DSPACE_INFO_LIST:
		req.u.mgmt_dspace_info_list.handle_count = 0;
		resp.u.mgmt_dspace_info_list.dspace_info_count = 0;
		reqsize = extra_size_PVFS_servreq_mgmt_dspace_info_list;
		respsize = extra_size_PVFS_servresp_mgmt_dspace_info_list;
		break;
	    case PVFS_SERV_MGMT_EVENT_MON:
		resp.u.mgmt_event_mon.event_count = 0;
		respsize = extra_size_PVFS_servresp_mgmt_event_mon;
		break;
	    case PVFS_SERV_PROTO_ERROR:
		/* nothing special */
		break;
	}
	/* since these take the max size when mallocing in the encode,
	 * give them a huge number, then later fix it. */
	max_size_array[i].req = max_size_array[i].resp = init_big_size;

	if (noreq)
	    reqsize = 0;
	else
	    reqsize += check_req_size(&req);

	respsize += check_resp_size(&resp);

	if (reqsize > init_big_size)
	    gossip_err("%s: op %d reqsize %d exceeded prealloced %d\n",
	      __func__, i, reqsize, init_big_size);
	if (respsize > init_big_size)
	    gossip_err("%s: op %d respsize %d exceeded prealloced %d\n",
	      __func__, i, respsize, init_big_size);
	max_size_array[i].req = reqsize;
	max_size_array[i].resp = respsize;
    }

    /* clean up stuff just used for initialization */
    free(tmp_dist.dist_name);
    free(tmp_name);
    initializing_sizes = 0;
}

/* lebf_encode_calc_max_size()
 *
 * reports the maximum allowed encoded size for the given request type
 *
 * returns size on success, -errno on failure
 */
static int lebf_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type)
{
    if(input_type == PINT_ENCODE_REQ)
	return(max_size_array[op_type].req);
    else if(input_type == PINT_ENCODE_RESP)
	return(max_size_array[op_type].resp);

    return(-EINVAL);
}

/*
 * Used by both encode functions, request and response, to set
 * up the one buffer which will hold the encoded message.
 */
static int
encode_common(struct PINT_encoded_msg *target_msg, int maxsize)
{
    int ret = 0;
    void *buf;

    /* this encoder always uses just one buffer */
    target_msg->buffer_list = &target_msg->buffer_stub;
    target_msg->size_list = &target_msg->size_stub;
    target_msg->list_count = 1;
    target_msg->buffer_type = BMI_PRE_ALLOC;
    /* allocate the max size buffer to avoid the work of calculating it */
    if (initializing_sizes)
	buf = malloc(maxsize);
    else
	buf = BMI_memalloc(target_msg->dest, maxsize, BMI_SEND);
    if (!buf) {
	ret = -ENOMEM;
	goto out;
    }

    target_msg->buffer_list[0] = buf;
    target_msg->ptr_current = buf;

    /* generic header */
    memcpy(target_msg->ptr_current, le_bytefield_table.generic_header,
	PINT_ENC_GENERIC_HEADER_SIZE);
    target_msg->ptr_current += PINT_ENC_GENERIC_HEADER_SIZE;

 out:
    return ret;
}

/* lebf_encode_req()
 *
 * encodes a request structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_req(
    struct PVFS_server_req *req,
    struct PINT_encoded_msg *target_msg)
{
    int ret = 0;
    char **p;

    ret = encode_common(target_msg, max_size_array[req->op].req);
    if (ret)
	goto out;

    /* every request has these fields */
    p = &target_msg->ptr_current;
    encode_PVFS_server_req(p, req);

#define CASE(tag,var) \
    case tag: encode_PVFS_servreq_##var(p,&req->u.var); break

    switch (req->op) {

	/* call standard function defined in headers */
	CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
	CASE(PVFS_SERV_CREATE, create);
	CASE(PVFS_SERV_REMOVE, remove);
	CASE(PVFS_SERV_MGMT_REMOVE_OBJECT, mgmt_remove_object);
	CASE(PVFS_SERV_MGMT_REMOVE_DIRENT, mgmt_remove_dirent);
	CASE(PVFS_SERV_IO, io);
	CASE(PVFS_SERV_GETATTR, getattr);
	CASE(PVFS_SERV_SETATTR, setattr);
	CASE(PVFS_SERV_CRDIRENT, crdirent);
	CASE(PVFS_SERV_RMDIRENT, rmdirent);
	CASE(PVFS_SERV_CHDIRENT, chdirent);
	CASE(PVFS_SERV_TRUNCATE, truncate);
	CASE(PVFS_SERV_MKDIR, mkdir);
	CASE(PVFS_SERV_READDIR, readdir);
	CASE(PVFS_SERV_FLUSH, flush);
	CASE(PVFS_SERV_STATFS, statfs);
	CASE(PVFS_SERV_MGMT_SETPARAM, mgmt_setparam);
	CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
	CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
	CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
	CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);

	case PVFS_SERV_GETCONFIG:
        case PVFS_SERV_MGMT_NOOP:
	case PVFS_SERV_PROTO_ERROR:
	    /* nothing else */
	    break;

	case PVFS_SERV_INVALID:
        case PVFS_SERV_WRITE_COMPLETION:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_JOB_TIMER:
	    gossip_err("%s: invalid operation %d\n", __func__, req->op);
	    ret = -ENOSYS;
	    break;
    }

#undef CASE

    /* although much more may have been allocated */
    target_msg->total_size = target_msg->ptr_current
      - (char *) target_msg->buffer_list[0];
    target_msg->size_list[0] = target_msg->total_size;

    if (target_msg->total_size > max_size_array[req->op].req) {
	ret = -ENOMEM;
	gossip_err("%s: op %d needed %Ld bytes but alloced only %d\n",
	  __func__, req->op, Ld(target_msg->total_size),
	  max_size_array[req->op].req);
    }

  out:
    return ret;
}


/* lebf_encode_resp()
 *
 * encodes a response structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_resp(
    struct PVFS_server_resp *resp,
    struct PINT_encoded_msg *target_msg)
{
    int ret;
    char **p;

    ret = encode_common(target_msg, max_size_array[resp->op].resp);
    if (ret)
	goto out;

    /* every response has these fields */
    p = &target_msg->ptr_current;
    encode_PVFS_server_resp(p, resp);

#define CASE(tag,var) \
    case tag: encode_PVFS_servresp_##var(p,&resp->u.var); break

    /* extra encoding rules for particular responses */
    switch (resp->op) {

	/* call standard function defined in headers */
	CASE(PVFS_SERV_GETCONFIG, getconfig);
	CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
	CASE(PVFS_SERV_CREATE, create);
	CASE(PVFS_SERV_IO, io);
	CASE(PVFS_SERV_GETATTR, getattr);
	CASE(PVFS_SERV_RMDIRENT, rmdirent);
	CASE(PVFS_SERV_CHDIRENT, chdirent);
	CASE(PVFS_SERV_MKDIR, mkdir);
	CASE(PVFS_SERV_READDIR, readdir);
	CASE(PVFS_SERV_STATFS, statfs);
	CASE(PVFS_SERV_MGMT_SETPARAM, mgmt_setparam);
	CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
	CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
	CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
	CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);
        CASE(PVFS_SERV_WRITE_COMPLETION, write_completion);

        case PVFS_SERV_REMOVE:
        case PVFS_SERV_MGMT_REMOVE_OBJECT:
        case PVFS_SERV_MGMT_REMOVE_DIRENT:
        case PVFS_SERV_SETATTR:
        case PVFS_SERV_CRDIRENT:
        case PVFS_SERV_TRUNCATE:
        case PVFS_SERV_FLUSH:
        case PVFS_SERV_MGMT_NOOP:
	case PVFS_SERV_PROTO_ERROR:
	    /* nothing else */
	    break;

	case PVFS_SERV_INVALID:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_JOB_TIMER:
	    gossip_err("%s: invalid operation %d\n", __func__, resp->op);
	    ret = -ENOSYS;
	    break;
    }

#undef CASE

    /* although much more may have been allocated */
    target_msg->total_size = target_msg->ptr_current
      - (char *) target_msg->buffer_list[0];
    target_msg->size_list[0] = target_msg->total_size;

    if (target_msg->total_size > max_size_array[resp->op].resp) {
	ret = -ENOMEM;
	gossip_err("%s: op %d needed %Ld bytes but alloced only %d\n",
	  __func__, resp->op, Ld(target_msg->total_size),
	  max_size_array[resp->op].resp);
    }

  out:
    return ret;
}

/* lebf_decode_req()
 *
 * decodes a request message
 *
 * input_buffer is a pointer past the generic header; it starts with
 * PVFS_server_req->op.
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_decode_req(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    PVFS_BMI_addr_t target_addr)
{
    int ret = 0;
    char *ptr = input_buffer;
    char **p = &ptr;
    struct PVFS_server_req *req = &target_msg->stub_dec.req;

    target_msg->buffer = req;

    /* decode generic part of request (enough to get op number) */
    decode_PVFS_server_req(p, req);

#define CASE(tag,var) \
    case tag: decode_PVFS_servreq_##var(p ,&req->u.var); break

    switch (req->op) {

	/* call standard function defined in headers */
	CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
	CASE(PVFS_SERV_CREATE, create);
	CASE(PVFS_SERV_REMOVE, remove);
	CASE(PVFS_SERV_MGMT_REMOVE_OBJECT, mgmt_remove_object);
	CASE(PVFS_SERV_MGMT_REMOVE_DIRENT, mgmt_remove_dirent);
	CASE(PVFS_SERV_IO, io);
	CASE(PVFS_SERV_GETATTR, getattr);
	CASE(PVFS_SERV_SETATTR, setattr);
	CASE(PVFS_SERV_CRDIRENT, crdirent);
	CASE(PVFS_SERV_RMDIRENT, rmdirent);
	CASE(PVFS_SERV_CHDIRENT, chdirent);
	CASE(PVFS_SERV_TRUNCATE, truncate);
	CASE(PVFS_SERV_MKDIR, mkdir);
	CASE(PVFS_SERV_READDIR, readdir);
	CASE(PVFS_SERV_FLUSH, flush);
	CASE(PVFS_SERV_STATFS, statfs);
	CASE(PVFS_SERV_MGMT_SETPARAM, mgmt_setparam);
	CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
	CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
	CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
	CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);

	case PVFS_SERV_GETCONFIG:
        case PVFS_SERV_MGMT_NOOP:
	    /* nothing else */
	    break;

	case PVFS_SERV_INVALID:
        case PVFS_SERV_WRITE_COMPLETION:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_JOB_TIMER:
	case PVFS_SERV_PROTO_ERROR:
	    gossip_lerr("%s: invalid operation %d.\n", __func__, req->op);
	    ret = -EPROTO;
	    goto out;
    }

#undef CASE

    if (ptr != (char *)input_buffer + input_size) {
	gossip_lerr("%s: improper input buffer size", __func__);
	ret = -EPROTO;
    }

  out:
    return(ret);
}

/* lebf_decode_resp()
 *
 * decodes a response structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_decode_resp(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    PVFS_BMI_addr_t target_addr)
{
    int ret = 0;
    char *ptr = input_buffer;
    char **p = &ptr;
    struct PVFS_server_resp *resp = &target_msg->stub_dec.resp;

    target_msg->buffer = resp;

    /* decode generic part of response (including op number) */
    decode_PVFS_server_resp(p, resp);

#define CASE(tag,var) \
    case tag: decode_PVFS_servresp_##var(p,&resp->u.var); break

    switch (resp->op) {

	/* call standard function defined in headers */
	CASE(PVFS_SERV_GETCONFIG, getconfig);
	CASE(PVFS_SERV_LOOKUP_PATH, lookup_path);
	CASE(PVFS_SERV_CREATE, create);
	CASE(PVFS_SERV_IO, io);
	CASE(PVFS_SERV_GETATTR, getattr);
	CASE(PVFS_SERV_RMDIRENT, rmdirent);
	CASE(PVFS_SERV_CHDIRENT, chdirent);
	CASE(PVFS_SERV_MKDIR, mkdir);
	CASE(PVFS_SERV_READDIR, readdir);
	CASE(PVFS_SERV_STATFS, statfs);
	CASE(PVFS_SERV_MGMT_SETPARAM, mgmt_setparam);
	CASE(PVFS_SERV_MGMT_PERF_MON, mgmt_perf_mon);
	CASE(PVFS_SERV_MGMT_ITERATE_HANDLES, mgmt_iterate_handles);
	CASE(PVFS_SERV_MGMT_DSPACE_INFO_LIST, mgmt_dspace_info_list);
	CASE(PVFS_SERV_MGMT_EVENT_MON, mgmt_event_mon);
        CASE(PVFS_SERV_WRITE_COMPLETION, write_completion);

        case PVFS_SERV_REMOVE:
        case PVFS_SERV_MGMT_REMOVE_OBJECT:
        case PVFS_SERV_MGMT_REMOVE_DIRENT:
        case PVFS_SERV_SETATTR:
        case PVFS_SERV_CRDIRENT:
        case PVFS_SERV_TRUNCATE:
        case PVFS_SERV_FLUSH:
        case PVFS_SERV_MGMT_NOOP:
        case PVFS_SERV_PROTO_ERROR:
	    /* nothing else */
	    break;

	case PVFS_SERV_INVALID:
        case PVFS_SERV_PERF_UPDATE:
        case PVFS_SERV_JOB_TIMER:
	    gossip_lerr("%s: invalid operation %d.\n", __func__, resp->op);
	    ret = -EPROTO;
	    goto out;
    }

#undef CASE

    if (ptr != (char *)input_buffer + input_size) {
	gossip_lerr("%s: improper input buffer size", __func__);
	ret = -EPROTO;
    }

  out:
    return(ret);
}

/* lebf_encode_rel()
 *
 * releases resources consumed while encoding
 *
 * no return value 
 */
static void lebf_encode_rel(
    struct PINT_encoded_msg *msg,
    enum PINT_encode_msg_type input_type)
{
    /* just a single buffer to free */
    if (initializing_sizes)
	free(msg->buffer_list[0]);
    else
	BMI_memfree(msg->dest, msg->buffer_list[0], msg->total_size, BMI_SEND);

    return;
}

/* lebf_decode_rel()
 *
 * releases resources consumed while decoding
 *
 * no return value
 */
static void lebf_decode_rel(struct PINT_decoded_msg *msg,
                            enum PINT_encode_msg_type input_type)
{
    if (input_type == PINT_DECODE_REQ) {
	struct PVFS_server_req *req = &msg->stub_dec.req;
	switch (req->op) {

	    case PVFS_SERV_CREATE:
		decode_free(req->u.create.handle_extent_array.extent_array);
		break;

	    case PVFS_SERV_IO:
		decode_free(req->u.io.io_dist);
		decode_free(req->u.io.file_req);
		break;

	    case PVFS_SERV_MKDIR:
		decode_free(req->u.mkdir.handle_extent_array.extent_array);
		if (req->u.mkdir.attr.mask & PVFS_ATTR_META_DIST)
		    decode_free(req->u.mkdir.attr.u.meta.dist);
		if (req->u.mkdir.attr.mask & PVFS_ATTR_META_DFILES)
		    decode_free(req->u.mkdir.attr.u.meta.dfile_array);
		break;

	    case PVFS_SERV_MGMT_DSPACE_INFO_LIST:
		decode_free(req->u.mgmt_dspace_info_list.handle_array);
		break;

	    case PVFS_SERV_SETATTR:
		if (req->u.setattr.attr.mask & PVFS_ATTR_META_DIST)
		    decode_free(req->u.setattr.attr.u.meta.dist);
		if (req->u.setattr.attr.mask & PVFS_ATTR_META_DFILES)
		    decode_free(req->u.setattr.attr.u.meta.dfile_array);
		break;

	    case PVFS_SERV_GETCONFIG:
	    case PVFS_SERV_LOOKUP_PATH:
	    case PVFS_SERV_REMOVE:
	    case PVFS_SERV_MGMT_REMOVE_OBJECT:
	    case PVFS_SERV_MGMT_REMOVE_DIRENT:
	    case PVFS_SERV_GETATTR:
	    case PVFS_SERV_CRDIRENT:
	    case PVFS_SERV_RMDIRENT:
	    case PVFS_SERV_CHDIRENT:
	    case PVFS_SERV_TRUNCATE:
	    case PVFS_SERV_READDIR:
	    case PVFS_SERV_FLUSH:
	    case PVFS_SERV_MGMT_SETPARAM:
	    case PVFS_SERV_MGMT_NOOP:
	    case PVFS_SERV_STATFS:
	    case PVFS_SERV_MGMT_ITERATE_HANDLES:
	    case PVFS_SERV_MGMT_PERF_MON:
	    case PVFS_SERV_MGMT_EVENT_MON:
		/* nothing to free */
		break;

	    case PVFS_SERV_INVALID:
	    case PVFS_SERV_WRITE_COMPLETION:
	    case PVFS_SERV_PERF_UPDATE:
	    case PVFS_SERV_JOB_TIMER:
	    case PVFS_SERV_PROTO_ERROR:
		gossip_lerr("%s: invalid request operation %d.\n",
		  __func__, req->op);
		break;
	}
    } else if (input_type == PINT_DECODE_RESP) {
	struct PVFS_server_resp *resp = &msg->stub_dec.resp;
	switch (resp->op) {

	    case PVFS_SERV_LOOKUP_PATH: {
		struct PVFS_servresp_lookup_path *lookup = &resp->u.lookup_path;
		decode_free(lookup->handle_array);
		decode_free(lookup->attr_array);
		break;
	    }

	    case PVFS_SERV_READDIR:
		decode_free(resp->u.readdir.dirent_array);
		break;

	    case PVFS_SERV_MGMT_PERF_MON:
		decode_free(resp->u.mgmt_perf_mon.perf_array);
		break;

	    case PVFS_SERV_MGMT_ITERATE_HANDLES:
		decode_free(resp->u.mgmt_iterate_handles.handle_array);
		break;

	    case PVFS_SERV_MGMT_DSPACE_INFO_LIST:
		decode_free(resp->u.mgmt_dspace_info_list.dspace_info_array);
		break;

	    case PVFS_SERV_GETATTR:
		if (resp->u.getattr.attr.mask & PVFS_ATTR_META_DIST)
		    decode_free(resp->u.getattr.attr.u.meta.dist);
		if (resp->u.getattr.attr.mask & PVFS_ATTR_META_DFILES)
		    decode_free(resp->u.getattr.attr.u.meta.dfile_array);
		break;

	    case PVFS_SERV_MGMT_EVENT_MON:
		decode_free(resp->u.mgmt_event_mon.event_array);
		break;

	    case PVFS_SERV_GETCONFIG:
	    case PVFS_SERV_CREATE:
	    case PVFS_SERV_REMOVE:
	    case PVFS_SERV_MGMT_REMOVE_OBJECT:
	    case PVFS_SERV_MGMT_REMOVE_DIRENT:
	    case PVFS_SERV_IO:
	    case PVFS_SERV_SETATTR:
	    case PVFS_SERV_CRDIRENT:
	    case PVFS_SERV_RMDIRENT:
	    case PVFS_SERV_CHDIRENT:
	    case PVFS_SERV_TRUNCATE:
	    case PVFS_SERV_MKDIR:
	    case PVFS_SERV_FLUSH:
	    case PVFS_SERV_MGMT_SETPARAM:
	    case PVFS_SERV_MGMT_NOOP:
	    case PVFS_SERV_STATFS:
	    case PVFS_SERV_WRITE_COMPLETION:
	    case PVFS_SERV_PROTO_ERROR:
		/* nothing to free */
		break;

	    case PVFS_SERV_INVALID:
	    case PVFS_SERV_PERF_UPDATE:
	    case PVFS_SERV_JOB_TIMER:
		gossip_lerr("%s: invalid response operation %d.\n",
		  __func__, resp->op);
		break;
	}
    }
}

static int
check_req_size(struct PVFS_server_req *req)
{
    struct PINT_encoded_msg msg;
    int size;

    lebf_encode_req(req, &msg);
    size = msg.total_size;
    lebf_encode_rel(&msg, 0);
    return size;
}

static int
check_resp_size(struct PVFS_server_resp *resp)
{
    struct PINT_encoded_msg msg;
    int size;

    lebf_encode_resp(resp, &msg);
    size = msg.total_size;
    lebf_encode_rel(&msg, 0);
    return size;
}

static PINT_encoding_functions lebf_functions = {
    lebf_encode_req,
    lebf_encode_resp,
    lebf_decode_req,
    lebf_decode_resp,
    lebf_encode_rel,
    lebf_decode_rel,
    lebf_encode_calc_max_size
};

PINT_encoding_table_values le_bytefield_table = {
    &lebf_functions,
    "little endian bytefield",
    lebf_initialize
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
