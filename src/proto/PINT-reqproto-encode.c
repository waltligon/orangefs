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
#include "pvfs2-debug.h"
#include "pvfs2-req-proto.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"
#include "bmi-byteswap.h"
#include "pint-event.h"
#include "id-generator.h"
#include "pvfs2-internal.h"

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

static PINT_encoding_table_values *PINT_encoding_table[
    ENCODING_TABLE_SIZE] = {NULL};

/* PINT_encode_initialize()
 *
 * starts up the protocol encoding interface
 *
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_encode_initialize(void)
{
    int ret = -PVFS_EINVAL;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"PINT_encode_initialize\n");
    if (ENCODING_IS_SUPPORTED(ENCODING_LE_BFIELD))
    {
        /* setup little endian bytefield encoding */
        PINT_encoding_table[ENCODING_LE_BFIELD] = &le_bytefield_table;
        le_bytefield_table.init_fun();

        /* header prepended to all messages of this type */
        *((int32_t*)&(le_bytefield_table.generic_header[0])) = 
            htobmi32(PVFS2_PROTO_VERSION);
        *((int32_t*)&(le_bytefield_table.generic_header[4])) = 
            htobmi32(ENCODING_LE_BFIELD);

        le_bytefield_table.enc_type = ENCODING_LE_BFIELD;
        ret = 0;
    }
    return ret;
}

/* PINT_encode_finalize()
 *
 * shuts down the protocol encoding interface
 *
 * no return value
 */
void PINT_encode_finalize(void)
{
    le_bytefield_table.finalize_fun();
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"PINT_encode_finalize\n");
    return;
}


/* PINT_encode()
 * 
 * encodes a buffer (containing a PVFS2 request or response) to be
 * sent over the network
 * 
 * returns 0 on success, -PVFS_error on failure
 */
int PINT_encode(void* input_buffer,
		enum PINT_encode_msg_type input_type,
		struct PINT_encoded_msg* target_msg,
		PVFS_BMI_addr_t target_addr,
		enum PVFS_encoding_type enc_type)
{
    int ret = -PVFS_EINVAL;
    target_msg->dest = target_addr;
    target_msg->enc_type = enc_type;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"PINT_encode\n");
    switch(enc_type)
    {
	case ENCODING_LE_BFIELD:
	    if (input_type == PINT_ENCODE_REQ)
	    {
		ret =  PINT_encoding_table[enc_type]->op->encode_req(
                    input_buffer, target_msg);
	    }
	    else if (input_type == PINT_ENCODE_RESP)
	    {
		ret =  PINT_encoding_table[enc_type]->op->encode_resp(
                    input_buffer, target_msg);
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
int PINT_decode(void* input_buffer,
		enum PINT_encode_msg_type input_type,
		struct PINT_decoded_msg* target_msg,
		PVFS_BMI_addr_t target_addr,
		PVFS_size size)
{
    int i=0;
    char* buffer_index = (char*)input_buffer + PINT_ENC_GENERIC_HEADER_SIZE;
    int size_index = (int)size - PINT_ENC_GENERIC_HEADER_SIZE;
    char* enc_type_ptr = (char*)input_buffer + 4;
    int ret;
    int32_t enc_type_recved, proto_ver_recved;
    int proto_major_recved, proto_minor_recved;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"PINT_decode\n");
    target_msg->enc_type = -1;  /* invalid */

    /* sanity check size */
    if(size < PINT_ENC_GENERIC_HEADER_SIZE)
    {
        gossip_err("Error: poorly formatted protocol message received.\n");
	gossip_err("   Too small: message only %lld bytes.\n",
	    lld(size));
	return(-PVFS_EPROTO);
    }
    
    /* pull the encoding type and protocol version out */
    proto_ver_recved = (int)bmitoh32(*((int32_t*)input_buffer));
    enc_type_recved = bmitoh32(*((int32_t*)enc_type_ptr));
    proto_major_recved = proto_ver_recved / 1000;
    proto_minor_recved = proto_ver_recved - (proto_major_recved*1000);

    /* check encoding type */
    if(enc_type_recved != ENCODING_LE_BFIELD)
    {
        gossip_err("Error: poorly formatted protocol message received.\n");
	gossip_err("   Encoding type mismatch: received type %d when "
	    "expecting %d.\n", (int)enc_type_recved, 
	    ENCODING_LE_BFIELD);
        return(-PVFS_EPROTONOSUPPORT);
    }

    /* check various protocol version possibilities */
    if(proto_major_recved != PVFS2_PROTO_MAJOR)
    {
        gossip_err("Error: poorly formatted protocol message received.\n");
	gossip_err("   Protocol version mismatch: received major version %d when "
	    "expecting %d.\n", (int)proto_major_recved,
	    PVFS2_PROTO_MAJOR);
	gossip_err("   Please verify your PVFS2 installation\n");
        gossip_err("   and make sure that the version is consistent.\n");
        return(-PVFS_EPROTONOSUPPORT);
    }

    if((input_type == PINT_DECODE_REQ) && 
        (proto_minor_recved > PVFS2_PROTO_MINOR))
    {
        gossip_err("Error: poorly formatted protocol message received.\n");
	gossip_err("   Protocol version mismatch: request has minor version %d when "
	    "expecting %d or lower.\n", (int)proto_minor_recved,
	    PVFS2_PROTO_MINOR);
        gossip_err("   Client is too new for server.\n");
	gossip_err("   Please verify your PVFS2 installation\n");
        gossip_err("   and make sure that the version is consistent.\n");
        return(-PVFS_EPROTONOSUPPORT);
    }

    if((input_type == PINT_DECODE_RESP) && 
        (proto_minor_recved < PVFS2_PROTO_MINOR))
    {
        gossip_err("Error: poorly formatted protocol message received.\n");
	gossip_err("   Protocol version mismatch: request has minor version %d when "
	    "expecting %d or higher.\n", (int)proto_minor_recved,
	    PVFS2_PROTO_MINOR);
        gossip_err("   Server is too old for client.\n");
	gossip_err("   Please verify your PVFS2 installation\n");
        gossip_err("   and make sure that the version is consistent.\n");
        return(-PVFS_EPROTONOSUPPORT);
    }

    for(i=0; i<ENCODING_TABLE_SIZE; i++)
    {
	if(PINT_encoding_table[i] && (PINT_encoding_table[i]->enc_type
            == enc_type_recved))
       	{
	    struct PVFS_server_req* tmp_req;
	    struct PVFS_server_req* tmp_resp;
	    target_msg->enc_type = enc_type_recved;
	    if(input_type == PINT_DECODE_REQ)
	    {
		ret = PINT_encoding_table[i]->op->decode_req(buffer_index,
		    size_index,
		    target_msg,
		    target_addr);
		tmp_req = target_msg->buffer;
		return(ret);
	    }
	    else if(input_type == PINT_DECODE_RESP)
	    {
		ret = PINT_encoding_table[i]->op->decode_resp(buffer_index,
		    size_index,
		    target_msg,
		    target_addr);
		tmp_resp = target_msg->buffer;
		return(ret);
	    }
	    else
	    {
		return(-PVFS_EINVAL);
	    }
	}
    }

    gossip_err("Error: poorly formatted protocol message received.\n");

    return(-PVFS_EPROTONOSUPPORT);
}
	
/* PINT_encode_release()
 *
 * frees all resources associated with a message that has been
 * encoded 
 *
 * no return value
 */
void PINT_encode_release(struct PINT_encoded_msg* input_buffer,
			 enum PINT_encode_msg_type input_type)
{ 
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"PINT_encode_release\n");
    if (ENCODING_IS_SUPPORTED(input_buffer->enc_type))
    {
        PINT_encoding_table[input_buffer->enc_type]->op->encode_release(
            input_buffer, input_type);
    }
    else
    {
        gossip_err("PINT_encode_release: Encoder type %d is not "
                   "supported.\n", input_buffer->enc_type);
    }
}

/* PINT_decode_release()
 *
 * frees all resources associated with a message that has been
 * decoded
 *
 * no return value
 */
void PINT_decode_release(struct PINT_decoded_msg* input_buffer,
			 enum PINT_encode_msg_type input_type)
{
    gossip_debug(GOSSIP_ENDECODE_DEBUG,"PINT_decode_release\n");
    if (ENCODING_IS_SUPPORTED(input_buffer->enc_type))
    {
        PINT_encoding_table[input_buffer->enc_type]->op->decode_release(
            input_buffer, input_type);
    }
    else if (input_buffer->enc_type == -1)
    {
        /* invalid return from PINT_decode, quietly return */
        ;
    }
    else
    {
        gossip_err("PINT_decode_release: Encoder type %d is not "
                   "supported.\n", input_buffer->enc_type);
    }
}


/* PINT_encode_calc_max_size()
 *
 * calculates maximum size of the encoded version of a protocol message.
 *
 * returns max size of encoded buffer on success, -PVFS_error on failure
 */
int PINT_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type,
    enum PVFS_encoding_type enc_type)
{    
    int ret = -PVFS_EINVAL;

    gossip_debug(GOSSIP_ENDECODE_DEBUG,"PINT_encode_calc_max_size\n");
    switch(enc_type)
    {
	case ENCODING_LE_BFIELD:
	    ret = PINT_encoding_table[enc_type]->op->encode_calc_max_size
		(input_type, op_type);
	    break;
	default:
	    gossip_lerr("Error: encoding type not supported.\n");
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
 * vim: ts=8 sts=4 sw=4 expandtab
 */
