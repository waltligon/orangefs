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

#include "src/proto/pvfs2-req-proto.h"
#include "bmi.h"

/* structure to describe messages that have been encoded */
struct PINT_encoded_msg
{
    PVFS_BMI_addr_t dest;             /* host this is going to */
    enum PVFS_encoding_type enc_type; /* type of encoding that was used */
    enum bmi_buffer_type buffer_type; /* buffer flag for BMI's use */
    void **buffer_list;               /* list of buffers */
    PVFS_size *size_list;             /* size of buffers */
    PVFS_size *alloc_size_list;       /* original size of buffers */
    int list_count;                   /* number of buffers */
    PVFS_size total_size;             /* aggregate size of encoding */

    /* fields below this comment are meant for internal use */
    char *ptr_current;                /* current encoding pointer */
    PVFS_size size_stub;              /* used for size_list */
    PVFS_size alloc_size_stub;        /* used for size_list */
    void *buffer_stub;                /* used for buffer_list */
};

/* structure to describe messages that have been decoded */
struct PINT_decoded_msg
{
    void *buffer;                     /* decoded buffer */
    enum PVFS_encoding_type enc_type; /* encoding type used */

    /* fields below this comment are meant for internal use */
    char *ptr_current;                /* current encoding pointer */

    /* used for storing decoded info */
    union
    {
        struct PVFS_server_req req;
        struct PVFS_server_resp resp;
    } stub_dec;
};

/* types of messages we will encode or decode */
enum PINT_encode_msg_type
{
    PINT_ENCODE_REQ = 1,
    PINT_ENCODE_RESP = 2
};

/* convenience, just for less weird looking arguments to decode functions */
#define PINT_DECODE_REQ PINT_ENCODE_REQ
#define PINT_DECODE_RESP PINT_ENCODE_RESP

/*******************************************************
 * public function prototypes
 */

int PINT_encode_initialize(void);

void PINT_encode_finalize(void);

int PINT_encode(
    void* input_buffer,
    enum PINT_encode_msg_type input_type,
    struct PINT_encoded_msg* target_msg,
    PVFS_BMI_addr_t target_addr,
    enum PVFS_encoding_type enc_type);

int PINT_decode(
    void* input_buffer,
    enum PINT_encode_msg_type input_type,
    struct PINT_decoded_msg* target_msg,
    PVFS_BMI_addr_t target_addr,
    PVFS_size size);

void PINT_encode_release(
    struct PINT_encoded_msg* msg,
    enum PINT_encode_msg_type input_type);

void PINT_decode_release(
    struct PINT_decoded_msg* msg,
    enum PINT_encode_msg_type input_type);

int PINT_encode_calc_max_size(
    enum PINT_encode_msg_type input_type,
    enum PVFS_server_op op_type,
    enum PVFS_encoding_type enc_type);


#endif /* __PINT_REQUEST_ENCODE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
