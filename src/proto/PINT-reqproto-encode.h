/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file defines the API for encoding and decoding the request
 * protocol that is used between clients and servers in pvfs2
 */

#ifndef __PINT_REQUEST_ENCODE_H
#define __PINT_REQUEST_ENCODE_H


/* structure to describe messages that have been encoded */
struct PINT_encoded_msg
{
	/* publicly accessable encoding information */
	/***********************************/
	/* Need to be set up ahead of time */
	/***********************************/
	bmi_addr_t dest;        /* host this is going to */
	bmi_flag_t buffer_flag; /* buffer flag for BMI's use */

	/* These values are filled in by the API */
	void** buffer_list;     /* list of buffers */
	PVFS_size* size_list;   /* size of buffers */
	int list_count;         /* number of buffers */
	PVFS_size total_size;   /* aggregate size of encoding */

	/* private encoding information goes here */
	int type;
};

/* structure to describe messages that have been decoded */
struct PINT_decoded_msg
{
	/* publicly accessable decoding information */
	void* buffer;

	/* private decoding information goes here */
};

/* types of messages we will encode or decode */
enum PINT_encode_msg_type
{
	PINT_ENCODE_REQ = 7,
	PINT_DECODE_REQ = 7,
	PINT_ENCODE_RESP = 13,
	PINT_DECODE_RESP = 13
};

#define ENCODING_TABLE_SIZE 5


/*******************************************************
 * public function prototypes
 */

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
	int type
	);

/* PINT_decode()
 *
 * decodes a buffer (containing a PVFS2 request or response) that
 * has been received from the network
 *
 * returns 0 on success, -ERRNO on failure
 */
int PINT_decode(
	void* input_buffer,
	enum PINT_encode_msg_type input_type,
	struct PINT_decoded_msg* target_msg,
	bmi_addr_t target_addr,
	PVFS_size size,
	int *type
	);
	
/* PINT_encode_release()
 *
 * frees all resources associated with a message that has been
 * encoded 
 *
 * no return value
 */
void PINT_encode_release(
	struct PINT_encoded_msg* msg,
	enum PINT_encode_msg_type input_type,
	int type
	);

/* PINT_decode_release()
 *
 * frees all resources associated with a message that has been
 * decoded
 *
 * no return value
 */
void PINT_decode_release(
	struct PINT_decoded_msg* msg,
	enum PINT_encode_msg_type input_type,
	int type
	);

#endif /* __PINT_REQUEST_ENCODE_H */

