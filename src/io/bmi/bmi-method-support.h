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
#include "pint-event.h"

#define BMI_MAX_CONTEXTS 16

/* magic number for BMI headers and control messages */
#define BMI_MAGIC_NR 51903

#ifndef timersub
# define timersub(a, b, result) \
  do { \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
    (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
    if ((result)->tv_usec < 0) { \
      --(result)->tv_sec; \
      (result)->tv_usec += 1000000; \
    } \
  } while (0)
#endif

/********************************************************
 * method interfaces and data structures 
 */

/* This is the generic address structure which contains adressing
 * information for every protocol we support.  The method routines
 * upcast the void* to find their particular device information.
 */
struct bmi_method_addr
{
    int method_type;
    int ref_count;
    void *method_data;		/* area to be used by specific methods */
    void *parent;               /* pointer back to generic BMI address info */  
    struct bmi_method_addr* primary;
    struct bmi_method_addr* secondary;
};
typedef struct bmi_method_addr *bmi_method_addr_p;

/* used to describe unexpected messages that arrive */
struct bmi_method_unexpected_info
{
    bmi_error_code_t error_code;
    bmi_method_addr_p addr;
    void *buffer;
    bmi_size_t size;
    bmi_msg_tag_t tag;
};

/* flags that can be set per method to affect behavior */
#define BMI_METHOD_FLAG_NO_POLLING 1

/* This is the table of interface functions that must be provided by BMI
 * methods.
 */
struct bmi_method_ops
{
    const char *method_name;
    int flags;
    int (*initialize) (bmi_method_addr_p, int, int);
    int (*finalize) (void);
    int (*set_info) (int, void *);
    int (*get_info) (int, void *);
    void *(*memalloc) (bmi_size_t, enum bmi_op_type);
    int (*memfree) (void *, bmi_size_t, enum bmi_op_type);

    int (*unexpected_free) (void *);

    int (*test) (bmi_op_id_t,
                 int *,
                 bmi_error_code_t *,
                 bmi_size_t *,
                 void **,
                 int,
                 bmi_context_id);
    int (*testsome) (int,
                     bmi_op_id_t *,
                     int *,
                     int *,
                     bmi_error_code_t *,
                     bmi_size_t *,
                     void **,
                     int,
                     bmi_context_id);
    int (*testcontext) (int,
                        bmi_op_id_t*,
                        int *,
                        bmi_error_code_t *,
                        bmi_size_t *,
                        void **,
                        int,
                        bmi_context_id);
    int (*testunexpected) (int,
                           int *,
                           struct bmi_method_unexpected_info *,
                           uint8_t,
                           int);
    bmi_method_addr_p (*method_addr_lookup) (const char *);
    int (*post_send_list) (bmi_op_id_t *,
                           bmi_method_addr_p,
                           const void *const *,
                           const bmi_size_t *,
                           int,
                           bmi_size_t,
                           enum bmi_buffer_type,
                           bmi_msg_tag_t,
                           void *,
                           bmi_context_id,
                           PVFS_hint hints);
    int (*post_recv_list) (bmi_op_id_t *,
                           bmi_method_addr_p,
                           void *const *,
                           const bmi_size_t *,
                           int,
                           bmi_size_t,
                           bmi_size_t *,
                           enum bmi_buffer_type,
                           bmi_msg_tag_t,
                           void *,
                           bmi_context_id,
                           PVFS_hint Hints);
    int (*post_sendunexpected_list) (bmi_op_id_t *,
                                     bmi_method_addr_p,
                                     const void *const *,
                                     const bmi_size_t *,
                                     int,
                                     bmi_size_t,
                                     enum bmi_buffer_type,
                                     bmi_msg_tag_t,
                                     uint8_t,
                                     void *,
                                     bmi_context_id,
                                     PVFS_hint hints);
    int (*open_context)(bmi_context_id);
    void (*close_context)(bmi_context_id);
    int (*cancel)(bmi_op_id_t, bmi_context_id);
    const char* (*rev_lookup_unexpected)(bmi_method_addr_p);
    int (*query_addr_range)(bmi_method_addr_p, const char *, int);
};


/*
 * This structure is somewhat optional.  TCP and GM use the elements in
 * here extensively, but IB, MX, Portals only use the bits required by
 * the generic BMI layer.  Those are op_id and addr.  Everything else is
 * ignored.  Would be nice to push most of method_op down into TCP and GM
 * so other BMI implementations do not need to drag around the unused fields.
 */
struct method_op
{
    bmi_op_id_t op_id;		/* operation identifier */
    enum bmi_op_type send_recv;	/* type of operation */
    void *user_ptr;		/* user_ptr associated with this op */
    bmi_msg_tag_t msg_tag;	/* message tag */
    uint8_t class;              /* message class (if unexpected) */
    bmi_error_code_t error_code;	/* final status of operation */
    bmi_size_t amt_complete;	/* how much is completed */
    bmi_size_t env_amt_complete;	/* amount of the envelope that is completed */
    void *buffer;		/* the memory region to transfer */
    bmi_size_t actual_size;	/* total size of the transfer */
    bmi_size_t expected_size;	/* expected size of the transfer */
    bmi_method_addr_p addr;	/* peer address involved in the communication */
    int mode;		/* operation mode */
    bmi_context_id context_id;  /* context */
    struct qlist_head op_list_entry;	/* op_list link */
    struct qlist_head hash_link;	/* hash table link */
    void *method_data;		/* for use by individual methods */

	/************************************************************
	 * following items were added for convenience of methods that 
	 * implement send_list and recv_list 
	 */
    void *const *buffer_list;		/* list of buffers */
    const bmi_size_t *size_list;	/* list of buffer sizes */
    int list_count;		/* # of items in buffer list */
    int list_index;		/* index of current buffer to xfer */
    /* how much is completed in current buffer */
    bmi_size_t cur_index_complete;
    PINT_event_id event_id;
};
typedef struct method_op method_op_st, *method_op_p;

struct method_drop_addr_query
{
    struct bmi_method_addr* addr;
    int response;
};

/***********************************************************
 * utility functions provided for use by the network methods 
 */

/* functions for managing operations */
method_op_p bmi_alloc_method_op(bmi_size_t payload_size);
void bmi_dealloc_method_op(method_op_p op_p);

/* These functions can be used to manage generic address structures */
bmi_method_addr_p bmi_alloc_method_addr(int method_type,
				bmi_size_t payload_size);
void bmi_dealloc_method_addr(bmi_method_addr_p old_method_addr);

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
 * vim: ts=8 sts=4 sw=4 expandtab
 */
