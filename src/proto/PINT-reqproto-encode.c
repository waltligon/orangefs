/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <errno.h>

#include "bmi.h"
#include "gossip.h"
#include "pvfs2-req-proto.h"
#include "PINT-reqproto-encode.h"
#include "PINT-reqproto-module.h"

#define ENCODING_TABLE_SIZE 5

extern PINT_encoding_table_values_s contig_buffer_table;

PINT_encoding_table_values_s *PINT_encoding_table[ENCODING_TABLE_SIZE] =
{
    &contig_buffer_table,
    NULL, // XDR?
    NULL
};

/* PINT_encode_initialize()
 *
 * starts up the protocol encoding interface
 *
 * returns 0 on success, -errno on failure
 */
int PINT_encode_initialize(void)
{
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


/* PINT_encode_init()
 */
int PINT_encode_init(void)
{
    int i=0;
    while(i++<ENCODING_TABLE_SIZE)
	if(PINT_encoding_table[i])
	    if(!PINT_encoding_table[i]->op)
		(PINT_encoding_table[i]->init_fun)();
    return 0;
}


/* PINT_encode()
 * 
 * encodes a buffer (containing a PVFS2 request or response) to be
 * sent over the network
 * 
 * returns 0 on success, -ERRNO on failure
 */
int PINT_encode(
		void* input_buffer,
		enum PINT_encode_msg_type input_type,
		struct PINT_encoded_msg* target_msg,
		bmi_addr_t target_addr,
		enum PINT_encoding_type enc_type
		)
{
    int ret=0;
    target_msg->dest = target_addr;
    target_msg->enc_type = enc_type;

    switch(enc_type)
    {
	case PINT_ENC_DIRECT:
	    if (input_type == PINT_ENCODE_REQ)
	    {
		ret =  PINT_encoding_table[enc_type]->op->encode_req(input_buffer,
								 target_msg,
								 ENCODED_HEADER_SIZE);
	    }
	    else if(input_type == PINT_ENCODE_RESP)
	    {
		ret =  PINT_encoding_table[enc_type]->op->encode_resp(input_buffer,
								  target_msg,
								  ENCODED_HEADER_SIZE);
	    }
	    break;
	default:
	    gossip_lerr("Error: encoding type not supported.\n");
	    ret = -EINVAL;
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
 * returns 0 on success, -ERRNO on failure
 */
int PINT_decode(
		void* input_buffer,
		enum PINT_encode_msg_type input_type,
		struct PINT_decoded_msg* target_msg,
		bmi_addr_t target_addr,
		PVFS_size size
		)
{
    int ret = -1;
    /* TODO: pull this from the input buffer... */
    enum PINT_encoding_type type = PINT_ENC_DIRECT;

    target_msg->enc_type = type;
    
    switch(type)
    {
	case PINT_ENC_DIRECT:
	    if (input_type == PINT_DECODE_REQ)
	    {
		ret = PINT_encoding_table[type]->op->decode_req(input_buffer,
								 (int)size,
								 target_msg,
								 target_addr);
	    }
	    else if(input_type == PINT_DECODE_RESP)
	    {
		ret = PINT_encoding_table[type]->op->decode_resp(input_buffer,
								  (int)size,
								  target_msg,
								  target_addr);
	    }
	    break;
	default:
	    gossip_lerr("Error: encoding type not supported.\n");
	    ret = -EINVAL;
	    break;
    }

    return(ret);
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
    PINT_encoding_table[input_buffer->enc_type]->op->encode_release(input_buffer,
						  input_type);
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
    PINT_encoding_table[input_buffer->enc_type]->op->decode_release(input_buffer,
						  input_type);
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
    enum PINT_encoding_type enc_type)
{    
    int ret = -1;

    switch(enc_type)
    {
	case PINT_ENC_DIRECT:
	    ret = PINT_encoding_table[enc_type]->op->encode_calc_max_size
		(input_type, op_type);
	    break;
	default:
	    gossip_lerr("Error: encoding type not supported.\n");
	    ret = -EINVAL;
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
