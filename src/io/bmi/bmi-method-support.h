/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* These are some support functions and data types shared across the
 * various BMI methods.
 */

#ifndef __BMI_METHOD_SUPPORT_H
#define __BMI_METHOD_SUPPORT_H

#include "quicklist.h"
#include "bmi-types.h"

#define BMI_MAX_CONTEXTS 16

/* magic number for BMI headers and control messages */
#define BMI_MAGIC_NR 51903

/********************************************************
 * method interfaces and data structures 
 */

enum
{
    BMI_OP_SEND = 1,
    BMI_OP_RECV
};

/* this is the generic address structure which contains adressing
 * information for every protocol we support.  The method routines
 * can look into the union to find necessary information for a given
 * device.
 */
struct method_addr
{
    bmi_flag_t method_type;
    /* indicates if the address is on the local machine (usually for 
     * server listening) */
    bmi_flag_t local_addr;
    void *method_data;		/* area to be used by specific methods */
};
typedef struct method_addr method_addr_st, *method_addr_p;

/* used to describe unexpected messages that arrive */
struct method_unexpected_info
{
    bmi_error_code_t error_code;
    method_addr_p addr;
    void *buffer;
    bmi_size_t size;
    bmi_msg_tag_t tag;
};

/* This is the table of interface functions that must be provided by BMI
 * methods.
 */
struct bmi_method_ops
{
    char *method_name;
    int (*BMI_meth_initialize) (method_addr_p,
				bmi_flag_t,
				bmi_flag_t);
    int (*BMI_meth_finalize) (void);
    int (*BMI_meth_set_info) (int,
			      void *);
    int (*BMI_meth_get_info) (int,
			      void *);
    void *(*BMI_meth_memalloc) (bmi_size_t,
				bmi_flag_t);
    int (*BMI_meth_memfree) (void *,
			     bmi_size_t,
			     bmi_flag_t);
    int (*BMI_meth_post_send) (bmi_op_id_t *,
			       method_addr_p,
			       void *,
			       bmi_size_t,
			       bmi_flag_t,
			       bmi_msg_tag_t,
			       void *,
			       bmi_context_id);
    int (*BMI_meth_post_sendunexpected) (bmi_op_id_t *,
					 method_addr_p,
					 void *,
					 bmi_size_t,
					 bmi_flag_t,
					 bmi_msg_tag_t,
					 void *,
					 bmi_context_id);
    int (*BMI_meth_post_recv) (bmi_op_id_t *,
			       method_addr_p,
			       void *,
			       bmi_size_t,
			       bmi_size_t *,
			       bmi_flag_t,
			       bmi_msg_tag_t,
			       void *,
			       bmi_context_id);
    int (*BMI_meth_test) (bmi_op_id_t,
			  int *,
			  bmi_error_code_t *,
			  bmi_size_t *,
			  void **,
			  int,
			  bmi_context_id);
    int (*BMI_meth_testsome) (int,
			      bmi_op_id_t *,
			      int *,
			      int *,
			      bmi_error_code_t *,
			      bmi_size_t *,
			      void **,
			      int,
			      bmi_context_id);
    int (*BMI_meth_testcontext) (int,
				bmi_op_id_t*,
				int *,
				bmi_error_code_t *,
				bmi_size_t *,
				void **,
				int,
				bmi_context_id);
    int (*BMI_meth_testunexpected) (int,
				    int *,
				    struct method_unexpected_info *,
				    int);
      method_addr_p(*BMI_meth_method_addr_lookup) (const char *);
    int (*BMI_meth_post_send_list) (bmi_op_id_t *,
				    method_addr_p,
				    void **,
				    bmi_size_t *,
				    int,
				    bmi_size_t,
				    bmi_flag_t,
				    bmi_msg_tag_t,
				    void *,
				    bmi_context_id);
    int (*BMI_meth_post_recv_list) (bmi_op_id_t *,
				    method_addr_p,
				    void **,
				    bmi_size_t *,
				    int,
				    bmi_size_t,
				    bmi_size_t *,
				    bmi_flag_t,
				    bmi_msg_tag_t,
				    void *,
				    bmi_context_id);
    int (*BMI_meth_post_sendunexpected_list) (bmi_op_id_t *,
					      method_addr_p,
					      void **,
					      bmi_size_t *,
					      int,
					      bmi_size_t,
					      bmi_flag_t,
					      bmi_msg_tag_t,
					      void *,
					      bmi_context_id);
    int (*BMI_meth_open_context)(bmi_context_id);
    void (*BMI_meth_close_context)(bmi_context_id);
};


/* this is the internal structure used to represent method operations */
struct method_op
{
    bmi_op_id_t op_id;		/* operation identifier */
    bmi_flag_t send_recv;	/* type of operation */
    void *user_ptr;		/* user_ptr associated with this op */
    bmi_msg_tag_t msg_tag;	/* message tag */
    bmi_error_code_t error_code;	/* final status of operation */
    bmi_size_t amt_complete;	/* how much is completed */
    bmi_size_t env_amt_complete;	/* amount of the envelope that is completed */
    void *buffer;		/* the memory region to transfer */
    bmi_size_t actual_size;	/* total size of the transfer */
    bmi_size_t expected_size;	/* expected size of the transfer */
    method_addr_st *addr;	/* peer address involved in the communication */
    bmi_flag_t mode;		/* operation mode */
    bmi_context_id context_id;  /* context */
    struct qlist_head op_list_entry;	/* op_list link */
    struct qlist_head hash_link;	/* hash table link */
    void *method_data;		/* for use by individual methods */

	/************************************************************
	 * following items were added for convenience of methods that 
	 * implement send_list and recv_list 
	 */
    void **buffer_list;		/* list of buffers */
    bmi_size_t *size_list;	/* list of buffer sizes */
    int list_count;		/* # of items in buffer list */
    int list_index;		/* index of current buffer to xfer */
    /* how much is completed in current buffer */
    bmi_size_t cur_index_complete;
};
typedef struct method_op method_op_st, *method_op_p;

/* generic method parameters */
struct method_params
{
    bmi_flag_t method_flags;
    bmi_flag_t method_id;
    method_addr_p listen_addr;
    /* message size limits: */
    bmi_size_t mode_immed_limit;
    bmi_size_t mode_eager_limit;
    bmi_size_t mode_rend_limit;
    bmi_size_t mode_unexp_limit;
};
typedef struct method_params method_params_st, *method_params_p;


/***********************************************************
 * utility functions provided for use by the network methods 
 */

/* functions for managing operations */
method_op_p alloc_method_op(bmi_size_t payload_size);
void dealloc_method_op(method_op_p op_p);

/* These functions can be used to manage generic address structures */
method_addr_p alloc_method_addr(bmi_flag_t method_type,
				bmi_size_t payload_size);
void dealloc_method_addr(method_addr_p old_method_addr);

/* string parsing utilities */
char *string_key(const char *key,
		 const char *id_string);

#endif /* __BMI_METHOD_SUPPORT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
