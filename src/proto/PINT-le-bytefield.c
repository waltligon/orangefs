/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include "bmi.h"
#include "bmi-byteswap.h"
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

#define PINT_ENC 1
#define PINT_DEC 2
#define PINT_CALC_SIZE 3

/**************************************************************
 * macros for transforming basic types
 */

/* unsigned 32 bit int */
#define PINT_XENC_UINT32(msg_p,x_p)				    \
do{								    \
    if(PINT_XENC_MODE == PINT_ENC){				    \
	*((uint32_t*)((msg_p)->ptr_current)) = htobmi32(*(x_p));    \
    }								    \
    else if(PINT_XENC_MODE == PINT_DEC){			    \
	*(x_p) = bmitoh32(*((uint32_t*)((msg_p)->ptr_current)));    \
    }								    \
    (msg_p)->ptr_current += sizeof(uint32_t);			    \
}while(0)

/* enum types */
#define PINT_XENC_ENUM PINT_XENC_UINT32

/* signed 32 bit int */
#define PINT_XENC_INT32(msg_p,x_p)				    \
do{								    \
    if(PINT_XENC_MODE == PINT_ENC){				    \
	*((int32_t*)((msg_p)->ptr_current)) = htobmi32(*(x_p));     \
    }								    \
    else if(PINT_XENC_MODE == PINT_DEC){			    \
	*(x_p) = bmitoh32(*((int32_t*)((msg_p)->ptr_current)));     \
    }								    \
    (msg_p)->ptr_current += sizeof(int32_t);			    \
}while(0)

/* unsigned 64 bit int */
#define PINT_XENC_UINT64(msg_p,x_p)				    \
do{								    \
    if(PINT_XENC_MODE == PINT_ENC){				    \
	*((uint64_t*)((msg_p)->ptr_current)) = htobmi64(*(x_p));    \
    }								    \
    else if(PINT_XENC_MODE == PINT_DEC){			    \
	*(x_p) = bmitoh64(*((uint64_t*)((msg_p)->ptr_current)));    \
    }								    \
    (msg_p)->ptr_current += sizeof(uint64_t);			    \
}while(0)

/* signed 64 bit int */
#define PINT_XENC_INT64(msg_p,x_p)				    \
do{								    \
    if(PINT_XENC_MODE == PINT_ENC){				    \
	*((int64_t*)((msg_p)->ptr_current)) = htobmi64(*(x_p));     \
    }								    \
    else if(PINT_XENC_MODE == PINT_DEC){			    \
	*(x_p) = bmitoh64(*((int64_t*)((msg_p)->ptr_current)));     \
    }								    \
    (msg_p)->ptr_current += sizeof(int64_t);			    \
}while(0)

/* strings */
#define PINT_XENC_STRING(msg_p,x_p,size)			    \
do{								    \
    if(PINT_XENC_MODE == PINT_ENC){				    \
	memcpy((msg_p)->ptr_current, x_p, size);		    \
    }								    \
    else if(PINT_XENC_MODE == PINT_DEC){			    \
	x_p = (msg_p)->ptr_current;				    \
    }								    \
    (msg_p)->ptr_current += size;				    \
}while(0)

/* PVFS2 specific types */
#define PINT_XENC_PVFS_ERROR	    PINT_XENC_INT32
#define PINT_XENC_PVFS_OFFSET	    PINT_XENC_INT64
#define PINT_XENC_PVFS_SIZE	    PINT_XENC_INT64
#define PINT_XENC_PVFS_MSG_TAG	    PINT_XENC_INT32
#define PINT_XENC_PVFS_CONTEXT_ID   PINT_XENC_INT32
#define PINT_XENC_PVFS_HANDLE	    PINT_XENC_UINT64
#define PINT_XENC_PVFS_FS_ID	    PINT_XENC_INT32
#define PINT_XENC_PVFS_DS_POSITION  PINT_XENC_INT32
#define PINT_XENC_PVFS_UID	    PINT_XENC_UINT32
#define PINT_XENC_PVFS_GID	    PINT_XENC_UINT32
#define PINT_XENC_PVFS_TIME	    PINT_XENC_INT64
#define PINT_XENC_PVFS_PERMISSIONS  PINT_XENC_UINT32
#define PINT_XENC_PVFS_DS_TYPE	    PINT_XENC_ENUM

/* TODO: fill these in */
#define PINT_XENC_PVFS_DIST	    do{}while(0)	    
#define PINT_XENC_PVFS_DFILES	    do{}while(0)

#define PINT_XENC_PVFS_OBJ_ATTR(msg_p,attr_p)			    \
do{								    \
    PINT_XENC_PVFS_UID(msg_p,&((attr_p)->owner));		    \
    PINT_XENC_PVFS_GID(msg_p,&((attr_p)->group));		    \
    PINT_XENC_PVFS_PERMISSIONS(msg_p,&((attr_p)->perms));	    \
    PINT_XENC_PVFS_TIME(msg_p,&((attr_p)->atime));		    \
    PINT_XENC_PVFS_TIME(msg_p,&((attr_p)->mtime));		    \
    PINT_XENC_PVFS_TIME(msg_p,&((attr_p)->ctime));		    \
    PINT_XENC_UINT32(msg_p,&((attr_p)->mask));			    \
    PINT_XENC_PVFS_DS_TYPE(msg_p,&((attr_p)->objtype));		    \
    if(attr_p->mask & PVFS_ATTR_META_DIST){			    \
    }								    \
    if(attr_p->mask & PVFS_ATTR_META_DFILES){			    \
    }								    \
    if(attr_p->mask & PVFS_ATTR_DATA_SIZE){			    \
	PINT_XENC_PVFS_SIZE(msg_p,&((attr_p)->u.data.size));	    \
    }								    \
    if(attr_p->mask & PVFS_ATTR_SYMLNK_TARGET){			    \
	PINT_XENC_UINT32(msg_p,&((attr_p)->u.sym.target_path_len)); \
	PINT_XENC_STRING(msg_p,(attr_p)->u.sym.target_path,	    \
	    (attr_p)->u.sym.target_path_len);			    \
    }								    \
}while(0)

/************************************************************
 * macros for transforming specific structures
 */

/* operates on the generic part of a request structure */
#define PINT_XENC_REQ_GEN(msg_p,req)			    \
do{							    \
    PINT_XENC_ENUM(msg_p,&((req)->op));			    \
    PINT_XENC_PVFS_UID(msg_p,&((req)->credentials.uid));    \
    PINT_XENC_PVFS_GID(msg_p,&((req)->credentials.gid));    \
}while(0)

/* operates on the generic part of a response structure */
#define PINT_XENC_RESP_GEN(msg_p,resp)			    \
do{							    \
    PINT_XENC_ENUM(msg_p,&((resp)->op));		    \
    PINT_XENC_PVFS_ERROR(msg_p,&((resp)->status));	    \
}while(0)

/* operates on a getconfig request */
#define PINT_XENC_REQ_GETCONFIG(msg_p,req) do{}while(0)

/* operates on a getconfig response */
#define PINT_XENC_RESP_GETCONFIG(msg_p,resp)		    \
do{							    \
    PINT_XENC_UINT32(msg_p,&((resp)->u.getconfig.fs_config_buf_size));	\
    PINT_XENC_UINT32(msg_p,&((resp)->u.getconfig.server_config_buf_size)); \
    PINT_XENC_STRING(msg_p, (resp)->u.getconfig.fs_config_buf, \
	(resp)->u.getconfig.fs_config_buf_size);		    \
    PINT_XENC_STRING(msg_p, (resp)->u.getconfig.server_config_buf, \
	(resp)->u.getconfig.server_config_buf_size);	    \
}while(0)

/* operates on a getattr request */
#define PINT_XENC_REQ_GETATTR(msg_p,req)		    \
do{							    \
    PINT_XENC_PVFS_HANDLE(msg_p,&((req)->u.getattr.handle)); \
    PINT_XENC_PVFS_FS_ID(msg_p,&((req)->u.getattr.fs_id)); \
    PINT_XENC_UINT32(msg_p,&((req)->u.getattr.attrmask));  \
}while(0);

/* operates on a getattr reponse */
#define PINT_XENC_RESP_GETATTR(msg_p,resp)		    \
do{							    \
    PINT_XENC_PVFS_OBJ_ATTR(msg_p,&((resp)->u.getattr.attr)); \
}while(0);

static int lebf_encode_req(
    struct PVFS_server_req *request,
    struct PINT_encoded_msg *target_msg);
static int lebf_encode_resp(
    struct PVFS_server_resp *response,
    struct PINT_encoded_msg *target_msg);
static int lebf_decode_resp(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    bmi_addr_t target_addr);
static int lebf_decode_req(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    bmi_addr_t target_addr);
static void lebf_decode_rel(
    struct PINT_decoded_msg *msg,
    enum PINT_encode_msg_type input_type);
static void lebf_encode_rel(
    struct PINT_encoded_msg *msg,
    enum PINT_encode_msg_type input_type);
static int lebf_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type);
static void lebf_initialize(
    void);
static int lebf_encode_alloc_resp(
    struct PVFS_server_resp *response,
    struct PINT_encoded_msg *target_msg);
static int lebf_encode_alloc_req(
    struct PVFS_server_req* request,
    struct PINT_encoded_msg* target_msg);
    

static PINT_encoding_functions_s lebf_functions = {
    lebf_encode_req,
    lebf_encode_resp,
    lebf_decode_req,
    lebf_decode_resp,
    lebf_encode_rel,
    lebf_decode_rel,
    lebf_encode_calc_max_size
};

PINT_encoding_table_values_s le_bytefield_table = {
    &lebf_functions,
    "little endian bytefield",
    lebf_initialize
};

/* an array of structs for storing precalculated maximum encoding sizes
 * for each type of server operation 
 */
struct max_size
{
    int max_req;
    int max_resp;
};
static struct max_size max_size_array[PVFS_MAX_SERVER_OP+1];

/* lebf_initialize()
 *
 * initializes the encoder module, calculates max sizes of each request type 
 * in advance
 *
 * no return value
 */
static void lebf_initialize(void)
{
    struct PVFS_server_req tmp_req;
    struct PVFS_server_resp tmp_resp;
    struct PINT_encoded_msg tmp_msg;
    int PINT_XENC_MODE = PINT_CALC_SIZE;

    le_bytefield_table.op = &lebf_functions;
    memset(max_size_array, 0, ((PVFS_MAX_SERVER_OP+1)*sizeof(struct max_size)));

    /* calculate maximum encoded message size for each type of operation */

    /* getconfig request */
    tmp_msg.ptr_current = 0;
    memset(&tmp_req, 0, sizeof(tmp_req));
    PINT_XENC_REQ_GEN(&tmp_msg, &tmp_req);
    PINT_XENC_REQ_GETCONFIG(&tmp_msg, &tmp_req);
    max_size_array[PVFS_SERV_GETCONFIG].max_req = (int)(tmp_msg.ptr_current);


    /* getconfig response */
    tmp_msg.ptr_current = 0;
    memset(&tmp_resp, 0, sizeof(tmp_resp));
    PINT_XENC_RESP_GEN(&tmp_msg, &tmp_resp);
    PINT_XENC_RESP_GETCONFIG(&tmp_msg, &tmp_resp);
    max_size_array[PVFS_SERV_GETCONFIG].max_resp = (int)(tmp_msg.ptr_current)
	+ (PVFS_REQ_LIMIT_CONFIG_FILE_BYTES * 2);

    return;
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
	return(max_size_array[op_type].max_req);
    else if(input_type == PINT_ENCODE_RESP)
	return(max_size_array[op_type].max_resp);

    return(-EINVAL);
}

/* lebf_encode_req()
 *
 * encodes a request structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_req(
    struct PVFS_server_req *request,
    struct PINT_encoded_msg *target_msg)
{
    int ret = -1;
    int PINT_XENC_MODE = PINT_ENC;

    /* this encoder always uses just one buffer */
    target_msg->buffer_list = &target_msg->buffer_stub;
    target_msg->size_list = &target_msg->size_stub;
    target_msg->list_count = 1;
    target_msg->buffer_type = BMI_PRE_ALLOC;

    /* compute size and allocate a buffer */
    ret = lebf_encode_alloc_req(request, target_msg);
    if(ret < 0)
    {
	return(ret);
    }

    /* tack on generic header */
    memcpy(target_msg->ptr_current, le_bytefield_table.generic_header,
	PINT_ENC_GENERIC_HEADER_SIZE);
    target_msg->ptr_current += PINT_ENC_GENERIC_HEADER_SIZE;

    switch(request->op)
    {
	case PVFS_SERV_GETCONFIG:
	    PINT_XENC_REQ_GEN(target_msg, request);
	    PINT_XENC_REQ_GETCONFIG(target_msg, request);
	    ret = 0;
	    break;
	default:
	    gossip_lerr("Error: unsupported operation.\n");
	    ret = -ENOSYS;
	    break;
    }


    /* check sanity */
    if(ret == 0)
    {
	assert(target_msg->total_size == (int)(target_msg->ptr_current -
	    (char*)(target_msg->buffer_list[0])));
    }
    return(ret);
}


/* lebf_encode_resp()
 *
 * encodes a response structure
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_resp(
    struct PVFS_server_resp *response,
    struct PINT_encoded_msg *target_msg)
{
    int ret = -1;
    int PINT_XENC_MODE = PINT_ENC;

    /* this encoder always uses just one buffer */
    target_msg->buffer_list = &target_msg->buffer_stub;
    target_msg->size_list = &target_msg->size_stub;
    target_msg->list_count = 1;
    target_msg->buffer_type = BMI_PRE_ALLOC;

    /* compute size and allocate a buffer */
    ret = lebf_encode_alloc_resp(response, target_msg);
    if(ret < 0)
    {
	return(ret);
    }

    /* tack on generic header */
    memcpy(target_msg->ptr_current, le_bytefield_table.generic_header,
	PINT_ENC_GENERIC_HEADER_SIZE);
    target_msg->ptr_current += PINT_ENC_GENERIC_HEADER_SIZE;

    switch(response->op)
    {
	case PVFS_SERV_GETCONFIG:
	    PINT_XENC_RESP_GEN(target_msg, response);
	    PINT_XENC_RESP_GETCONFIG(target_msg, response);
	    ret = 0;
	    break;
	default:
	    gossip_lerr("Error: unsupported operation.\n");
	    ret = -ENOSYS;
	    break;
    }

    /* check sanity */
    if(ret == 0)
    {
	assert(target_msg->total_size == (int)(target_msg->ptr_current -
	    (char*)(target_msg->buffer_list[0])));
    }
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
    bmi_addr_t target_addr)
{
    int ret = -1;
    int PINT_XENC_MODE = PINT_DEC;

    target_msg->buffer = &(target_msg->stub_dec.resp);
    target_msg->ptr_current = input_buffer;

    /* decode generic part of response (enough to get op number) */
    PINT_XENC_RESP_GEN(target_msg, &(target_msg->stub_dec.resp));

    switch(target_msg->stub_dec.resp.op)
    {
	case PVFS_SERV_GETCONFIG:
	    PINT_XENC_RESP_GETCONFIG(target_msg, &(target_msg->stub_dec.resp));
	    ret = 0;
	    break;
	default:
	    gossip_lerr("Error: unkown server operation: %d\n",
		(int)target_msg->stub_dec.req.op);
	    ret = -EPROTO;
	    break;
    }

    return(ret);
}


/* lebf_decode_req()
 *
 * decodes a request message
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_decode_req(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    bmi_addr_t target_addr)
{
    int ret = -1;
    int PINT_XENC_MODE = PINT_DEC;

    target_msg->buffer = &(target_msg->stub_dec.req);
    target_msg->ptr_current = input_buffer;

    /* decode generic part of request (enough to get op number) */
    PINT_XENC_REQ_GEN(target_msg, &(target_msg->stub_dec.req));

    switch(target_msg->stub_dec.req.op)
    {
	case PVFS_SERV_GETCONFIG:
	    PINT_XENC_REQ_GETCONFIG(target_msg, &(target_msg->stub_dec.req));
	    ret = 0;
	    break;
	default:
	    gossip_lerr("Error: unkown server operation: %d\n",
		(int)target_msg->stub_dec.req.op);
	    ret = -EPROTO;
	    break;
    }

    return(ret);
}


/* lebf_decode_rel()
 *
 * releases resources consumed while decoding
 *
 * no return value
 */
static void lebf_decode_rel(
    struct PINT_decoded_msg *msg,
    enum PINT_encode_msg_type input_type)
{
    if(input_type == PINT_ENCODE_REQ)
    {
	switch(msg->stub_dec.req.op)
	{
	    case PVFS_SERV_GETCONFIG:
		break;
	    default:
		gossip_lerr("Error: unsupported operation.\n");
		break;
	}
    }
    else if(input_type == PINT_ENCODE_RESP)
    {
	switch(msg->stub_dec.resp.op)
	{
	    case PVFS_SERV_GETCONFIG:
		break;
	    default:
		gossip_lerr("Error: unsupported operation.\n");
		break;
	}
    }

    return;
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
    BMI_memfree(msg->dest, msg->buffer_list[0], msg->total_size, BMI_SEND);

    return;
}

/* lebf_encode_alloc_resp()
 *
 * internal function, calculates size needed for encoded version 
 * of a response and allocates the buffer
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_alloc_resp(
    struct PVFS_server_resp* response,
    struct PINT_encoded_msg* target_msg)
{
    int PINT_XENC_MODE = PINT_CALC_SIZE;

    target_msg->ptr_current = NULL;
    target_msg->total_size = 0;

    PINT_XENC_RESP_GEN(target_msg, response);

    switch(response->op)
    {
	case PVFS_SERV_GETCONFIG:
	    PINT_XENC_RESP_GETCONFIG(target_msg, response);
	    target_msg->total_size = (int)(target_msg->ptr_current) + 
		PINT_ENC_GENERIC_HEADER_SIZE;
	    break;
	default:
	    gossip_lerr("Error: unsupported operation.\n");
	    return(-ENOSYS);
	    break;
    }

    target_msg->size_list[0] = target_msg->total_size;
    target_msg->buffer_list[0] = 
	BMI_memalloc(target_msg->dest, target_msg->total_size,
	BMI_SEND);
    if(!target_msg->buffer_list[0])
    {
	return(-ENOMEM);
    }

    target_msg->ptr_current = target_msg->buffer_list[0];
    return(0);
}

/* lebf_encode_alloc_req()
 *
 * internal function, calculates size needed for encoded version 
 * of a request and allocates the buffer
 *
 * returns 0 on success, -errno on failure
 */
static int lebf_encode_alloc_req(
    struct PVFS_server_req* request,
    struct PINT_encoded_msg* target_msg)
{
    int PINT_XENC_MODE = PINT_CALC_SIZE;

    target_msg->ptr_current = NULL;
    target_msg->total_size = 0;

    PINT_XENC_REQ_GEN(target_msg, request);

    switch(request->op)
    {
	case PVFS_SERV_GETCONFIG:
	    PINT_XENC_REQ_GETCONFIG(target_msg, request);
	    target_msg->total_size = (int)(target_msg->ptr_current) + 
		PINT_ENC_GENERIC_HEADER_SIZE;
	    break;
	default:
	    gossip_lerr("Error: unsupported operation.\n");
	    return(-ENOSYS);
	    break;
    }

    target_msg->size_list[0] = target_msg->total_size;
    target_msg->buffer_list[0] = 
	BMI_memalloc(target_msg->dest, target_msg->total_size,
	BMI_SEND);
    if(!target_msg->buffer_list[0])
    {
	return(-ENOMEM);
    }

    target_msg->ptr_current = target_msg->buffer_list[0];
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
