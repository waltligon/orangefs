/* File to be included for modules of the Encoding Library */


#ifndef PINT_ENCODING_MODULE_H

/* Macros for function declarations */

#define ENC_RESP_ARGS struct PVFS_server_resp_s *response, \
							 struct PINT_encoded_msg *target_msg, \
							 int header_size\

#define ENC_REQ_ARGS struct PVFS_server_req_s *request, \
							struct PINT_encoded_msg *target_msg, \
							int header_size\

#define DEC_RESP_ARGS void *input_buffer, \
							 struct PINT_decoded_msg *target_msg, \
							 bmi_addr_t target_addr 

#define DEC_REQ_ARGS void *input_buffer, \
							struct PINT_decoded_msg *target_msg, \
							bmi_addr_t target_addr 

#define DEC_REL_ARGS struct PINT_decoded_msg *msg, \
							enum PINT_encode_msg_type input_type

#define ENC_REL_ARGS struct PINT_encoded_msg *msg, \
							enum PINT_encode_msg_type input_type

#define ENCODE_RESP_HEAD(__name) int __name(ENC_RESP_ARGS)
#define ENCODE_REQ_HEAD(__name) int __name(ENC_REQ_ARGS)
#define DECODE_RESP_HEAD(__name) int __name(DEC_RESP_ARGS)
#define DECODE_REQ_HEAD(__name) int __name(DEC_REQ_ARGS)
#define DECODE_REL_HEAD(__name) void __name(DEC_REL_ARGS)
#define ENCODE_REL_HEAD(__name) void __name(ENC_REL_ARGS)

/* Structs used to define the interface to the module. */

typedef struct PINT_encoding_functions
{
	int (*encode_req)(ENC_REQ_ARGS);
	int (*encode_resp)(ENC_RESP_ARGS);
	int (*decode_req)(DEC_REQ_ARGS);
	int (*decode_resp)(DEC_RESP_ARGS);
	void (*encode_release)(ENC_REL_ARGS);
	void (*decode_release)(DEC_REL_ARGS);
	int (*encode_gen_ack_sz)(int);
} PINT_encoding_functions_s;

typedef struct PINT_encoding_table_values
{
	PINT_encoding_functions_s *op;
	char *name;
	void (*init_fun)(void);
} PINT_encoding_table_values_s;

#endif /* PINT_ENCODING_MODULE_H */
