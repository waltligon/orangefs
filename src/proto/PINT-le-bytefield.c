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
#include "pvfs2-req-proto.h"
#include "gossip.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

#define PINT_ENC 1
#define PINT_DEC 2
#define PINT_CALC_SIZE 3

/* unsigned 32 bit int */
#define PINT_XENC_UINT32(msg_p,x_p)			    \
do{							    \
    if(PINT_XENC_MODE == PINT_ENC){			    \
	*((uint32_t*)((msg_p)->ptr_current)) = *(x_p);	    \
    }							    \
    else if(PINT_XENC_MODE == PINT_DEC){		    \
	*(x_p) = *((uint32_t*)((msg_p)->ptr_current));	    \
    }							    \
    (msg_p)->ptr_current += sizeof(uint32_t);		    \
}while(0)

/* operates on the generic part of a request structure */
#define PINT_XENC_REQ_GEN(msg_p,req)			    \
do{							    \
    PINT_XENC_UINT32(msg_p,&((req)->op));		    \
    PINT_XENC_UINT32(msg_p,&((req)->credentials.uid));	    \
    PINT_XENC_UINT32(msg_p,&((req)->credentials.gid));	    \
    PINT_XENC_UINT32(msg_p,&((req)->credentials.perms));    \
}while(0)

/* operates on a getconfig request */
#define PINT_XENC_REQ_GETCONFIG(msg_p,req)		    \
do{}while(0)

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
    struct PVFS_server_req* tmp_req = NULL;
    struct PINT_encoded_msg tmp_msg;
    int PINT_XENC_MODE = PINT_CALC_SIZE;

    le_bytefield_table.op = &lebf_functions;
    memset(max_size_array, 0, ((PVFS_MAX_SERVER_OP+1)*sizeof(struct max_size)));

    /* calculate maximum encoded message size for each type of operation */

    /* getconfig request */
    tmp_msg.ptr_current = 0;
    PINT_XENC_REQ_GEN(&tmp_msg, tmp_req);
    PINT_XENC_REQ_GETCONFIG(&tmp_msg, tmp_req);
    max_size_array[PVFS_SERV_GETCONFIG].max_req = (int)(tmp_msg.ptr_current);

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
    gossip_lerr("Error: unimplemented.\n");
    return(-ENOSYS);
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

    /* set size of buffer needed, although some requests may override this */
    target_msg->size_list[0] = target_msg->total_size = 
	max_size_array[request->op].max_req;

    switch(request->op)
    {
	case PVFS_SERV_GETCONFIG:
	    /* create a buffer */
	    target_msg->buffer_list[0] = target_msg->ptr_current =  
		BMI_memalloc(target_msg->dest, target_msg->total_size, BMI_SEND);
	    if(!target_msg->buffer_list[0])
		ret = -ENOMEM;

	    /* encode */
	    PINT_XENC_REQ_GEN(target_msg, request);
	    PINT_XENC_REQ_GETCONFIG(target_msg, request);

	    /* check sanity */
	    assert(target_msg->total_size == (int)(target_msg->ptr_current -
		(char*)(target_msg->buffer_list[0])));
	    ret = 0;
	    break;
	default:
	    gossip_lerr("Error: unsupported operation.\n");
	    ret = -ENOSYS;
	    break;
    }


    return(ret);
}

static int lebf_encode_resp(
    struct PVFS_server_resp *response,
    struct PINT_encoded_msg *target_msg)
{
    gossip_lerr("Error: unimplemented.\n");
    return(-ENOSYS);
}

static int lebf_decode_resp(
    void *input_buffer,
    int input_size,
    struct PINT_decoded_msg *target_msg,
    bmi_addr_t target_addr)
{
    gossip_lerr("Error: unimplemented.\n");
    return(-ENOSYS);
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

static void lebf_decode_rel(
    struct PINT_decoded_msg *msg,
    enum PINT_encode_msg_type input_type)
{
    gossip_lerr("Error: unimplemented.\n");
    return;
}

static void lebf_encode_rel(
    struct PINT_encoded_msg *msg,
    enum PINT_encode_msg_type input_type)
{
    gossip_lerr("Error: unimplemented.\n");
    return;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
