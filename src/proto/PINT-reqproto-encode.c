/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "bmi.h"
#include "gossip.h"
#include "pvfs2-req-proto.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"
#include "bmi-byteswap.h"
#include "pint-event.h"
#include "id-generator.h"

#define ENCODING_TABLE_SIZE 5

/* macros for logging encode and decode events */
#define ENCODE_EVENT_START(__enctype, __reqtype, __ptr) \
do { \
    PVFS_id_gen_t __tmp_id; \
    id_gen_fast_register(&__tmp_id, (__ptr)); \
    PINT_event_timestamp(__enctype, \
    (int32_t)(__reqtype), 0, __tmp_id, \
    PVFS_EVENT_FLAG_START); \
} while(0)
#define ENCODE_EVENT_STOP(__enctype, __reqtype, __ptr, __size) \
do { \
    PVFS_id_gen_t __tmp_id; \
    id_gen_fast_register(&__tmp_id, (__ptr)); \
    PINT_event_timestamp(__enctype, \
    (int32_t)(__reqtype), (__size), __tmp_id, \
    PVFS_EVENT_FLAG_END); \
} while(0)

extern PINT_encoding_table_values le_bytefield_table;
int g_admin_mode = 0;

static PINT_encoding_table_values *PINT_encoding_table[ENCODING_TABLE_SIZE] = {NULL};

/* PINT_encode_initialize()
 *
 * starts up the protocol encoding interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_encode_initialize(void)
{

    /* setup little endian bytefield encoding */
    PINT_encoding_table[ENCODING_LE_BFIELD] = &le_bytefield_table;
    le_bytefield_table.init_fun();
    /* header prepended to all messages of this type */
    *((int32_t*)&(le_bytefield_table.generic_header[0])) = 
	htobmi32(PVFS_RELEASE_NR);
    *((int32_t*)&(le_bytefield_table.generic_header[4])) = 
	htobmi32(ENCODING_LE_BFIELD);

    return(0);
}

/* PINT_encode_finalize()
 *
 * shuts down the protocol encoding interface
 *
 * no return value
 */
void PINT_encode_finalize(void)
{
    return;
}


/* PINT_encode()
 * 
 * encodes a buffer (containing a PVFS2 request or response) to be
 * sent over the network
 * 
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_encode(
		void* input_buffer,
		enum PINT_encode_msg_type input_type,
		struct PINT_encoded_msg* target_msg,
		PVFS_BMI_addr_t target_addr,
		enum PVFS_encoding_type enc_type
		)
{
    int ret = -1;
    target_msg->dest = target_addr;
    target_msg->enc_type = enc_type;

    switch(enc_type)
    {
	case ENCODING_LE_BFIELD:
	    if (input_type == PINT_ENCODE_REQ)
	    {
		/* overwrite flags on the way through */
		struct PVFS_server_req* tmp_req = input_buffer;
		tmp_req->flags = 0;
		if(g_admin_mode)
		    tmp_req->flags += PVFS_SERVER_REQ_ADMIN_MODE;
		ENCODE_EVENT_START(PVFS_EVENT_API_ENCODE_REQ,
		    tmp_req->op, tmp_req);
		ret =  PINT_encoding_table[enc_type]->op->encode_req(input_buffer,
								 target_msg);
		ENCODE_EVENT_STOP(PVFS_EVENT_API_ENCODE_REQ,
		    tmp_req->op, tmp_req, target_msg->total_size);
	    }
	    else if(input_type == PINT_ENCODE_RESP)
	    {
		struct PVFS_server_resp* tmp_resp = input_buffer;
		ENCODE_EVENT_START(PVFS_EVENT_API_ENCODE_RESP,
		    tmp_resp->op, tmp_resp);
		ret =  PINT_encoding_table[enc_type]->op->encode_resp(input_buffer,
								  target_msg);
		ENCODE_EVENT_STOP(PVFS_EVENT_API_ENCODE_RESP,
		    tmp_resp->op, tmp_resp, target_msg->total_size);
	    }
	    break;
	default:
	    gossip_lerr("Error: encoding type not supported.\n");
	    ret = -PVFS_EINVAL;
	    break;
    }

    return(ret);
}

/* PINT_decode()
 *
 * decodes a buffer (containing a PVFS2 request or response) that
 * has been received from the network
 *
 * Parameters:
 * input_buffer - encoded input
 * input_type   - PINT_DECODE_REQ or PINT_DECODE_RESP
 * target_msg   - pointer to struct PINT_decoded_msg, hold pointer to
 *                allocated memory holding decoded message, etc.
 * size         - size of encoded input
 *
 * Notes:
 * - One must call PINT_decode_release(target_msg, input_type, 0)
 *   in order for the memory allocated during the decode process to be
 *   freed.
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_decode(
		void* input_buffer,
		enum PINT_encode_msg_type input_type,
		struct PINT_decoded_msg* target_msg,
		PVFS_BMI_addr_t target_addr,
		PVFS_size size
		)
{
    int i=0;
    char* buffer_index = (char*)input_buffer + PINT_ENC_GENERIC_HEADER_SIZE;
    int size_index = (int)size - PINT_ENC_GENERIC_HEADER_SIZE;
    char* enc_type_ptr = (char*)input_buffer + 4;
    int ret;
    int32_t enc_type_recved, proto_ver_recved;

    /* compare the header of the incoming buffer against the precalculated
     * header associated with each module
     */
    for(i=0; i<ENCODING_TABLE_SIZE; i++)
    {
	if(PINT_encoding_table[i] && !(memcmp(input_buffer, 
	    PINT_encoding_table[i]->generic_header, 
	    PINT_ENC_GENERIC_HEADER_SIZE)))
	{
	    struct PVFS_server_req* tmp_req;
	    struct PVFS_server_req* tmp_resp;
	    target_msg->enc_type = bmitoh32(*((int32_t*)enc_type_ptr));
	    if(input_type == PINT_DECODE_REQ)
	    {
		ENCODE_EVENT_START(PVFS_EVENT_API_DECODE_REQ,
		    0, input_buffer);
		ret = PINT_encoding_table[i]->op->decode_req(buffer_index,
		    size_index,
		    target_msg,
		    target_addr);
		tmp_req = target_msg->buffer;
		ENCODE_EVENT_STOP(PVFS_EVENT_API_DECODE_REQ,
		    tmp_req->op, input_buffer, size);
		return(ret);
	    }
	    else if(input_type == PINT_DECODE_RESP)
	    {
		ENCODE_EVENT_START(PVFS_EVENT_API_DECODE_RESP,
		    0, input_buffer);
		ret = PINT_encoding_table[i]->op->decode_resp(buffer_index,
		    size_index,
		    target_msg,
		    target_addr);
		tmp_resp = target_msg->buffer;
		ENCODE_EVENT_STOP(PVFS_EVENT_API_DECODE_RESP,
		    tmp_resp->op, input_buffer, size);
		return(ret);
	    }
	    else
	    {
		return(-PVFS_EINVAL);
	    }
	}
    }

    gossip_err("Error: poorly formatted protocol message received.\n");

    enc_type_recved = bmitoh32(*((int32_t*)enc_type_ptr));
    proto_ver_recved = (int)bmitoh32(*((int32_t*)input_buffer));

    if(size < PINT_ENC_GENERIC_HEADER_SIZE)
    {
	gossip_err("   Too small: message only %Ld bytes.\n",
	    Ld(size));
	return(-PVFS_EPROTO);
    }

    if(enc_type_recved != ENCODING_LE_BFIELD)
    {
	gossip_err("   Encoding type mismatch: received type %d when "
	    "expecting %d.\n", (int)enc_type_recved, 
	    ENCODING_LE_BFIELD);
    }

    if(proto_ver_recved != PVFS_RELEASE_NR)
    {
	gossip_err("   Protocol version mismatch: received version %d when "
	    "expecting version %d.\n", (int)proto_ver_recved,
	    PVFS_RELEASE_NR);
	gossip_err("   Please verify your PVFS2 installation and make sure "
	"that the version is\n   consistent.\n");
    }

    return(-PVFS_EPROTONOSUPPORT);
}
	
/* PINT_encode_release()
 *
 * frees all resources associated with a message that has been
 * encoded 
 *
 * no return value
 */
void PINT_encode_release(
			 struct PINT_encoded_msg* input_buffer,
			 enum PINT_encode_msg_type input_type
			 )
{ 
    PINT_encoding_table[input_buffer->enc_type]->op->encode_release(
	input_buffer, input_type);

    return;
}

/* PINT_decode_release()
 *
 * frees all resources associated with a message that has been
 * decoded
 *
 * no return value
 */
void PINT_decode_release(
			 struct PINT_decoded_msg* input_buffer,
			 enum PINT_encode_msg_type input_type
			 )
{
    PINT_encoding_table[input_buffer->enc_type]->op->decode_release(
	input_buffer, input_type);
    
    return;
}


/* PINT_encode_calc_max_size()
 *
 * calculates maximum size of the encoded version of a protocol message.
 *
 * returns max size of encoded buffer on success, -errno on failure
 */
int PINT_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type,
    enum PVFS_encoding_type enc_type)
{    
    int ret = -1;

    switch(enc_type)
    {
	case ENCODING_LE_BFIELD:
	    ret = PINT_encoding_table[enc_type]->op->encode_calc_max_size
		(input_type, op_type);
	    break;
	default:
	    gossip_lerr("Error: encoding type not supported.\n");
	    ret = -PVFS_EINVAL;
	    break;
    }

    return(ret);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
