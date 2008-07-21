/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* File to be included for modules of the Encoding Library */

#ifndef PINT_ENCODING_MODULE_H
#define PINT_ENCODING_MODULE_H

/*defines the interface to the encoding modules */
typedef struct
{
    int (*encode_req) (
	struct PVFS_server_req * request,
	struct PINT_encoded_msg * target_msg);
    int (*encode_resp) (
	struct PVFS_server_resp * response,
	struct PINT_encoded_msg * target_msg);
    int (*decode_req) (
	void *input_buffer,
	int input_size,
	struct PINT_decoded_msg * target_msg,
	PVFS_BMI_addr_t target_addr);
    int (*decode_resp) (
	void *input_buffer,
	int input_size,
	struct PINT_decoded_msg * target_msg,
	PVFS_BMI_addr_t target_addr);
    void (*encode_release) (
	struct PINT_encoded_msg * msg,
	enum PINT_encode_msg_type input_type);
    void (*decode_release) (
	struct PINT_decoded_msg * msg,
	enum PINT_encode_msg_type input_type);
    int (*encode_calc_max_size) (
	enum PINT_encode_msg_type input_type,
	enum PVFS_server_op op_type);
} PINT_encoding_functions;

/* size of generic header placed at the beginning of all encoded buffers;
 * indicates encoding type and protocol version
 */
#define PINT_ENC_GENERIC_HEADER_SIZE 8

typedef struct
{
    PINT_encoding_functions *op;
    const char *name;
    void (*init_fun) (void);
    void (*finalize_fun) (void);
    char generic_header[PINT_ENC_GENERIC_HEADER_SIZE];
    int enc_type;
} PINT_encoding_table_values;

/* Defined encoders */
extern PINT_encoding_table_values le_bytefield_table;

#endif /* PINT_ENCODING_MODULE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
