/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* linked list implementation based on quicklist; used for storing network
 * operations 
 *
 * this is provided for use by network method implementations
 */

#ifndef __OP_LIST_H
#define __OP_LIST_H

#include "quicklist.h"
#include "bmi-types.h"
#include "bmi-method-support.h"

typedef struct qlist_head *op_list_p;

/* these are the search parameters that may be used */
struct op_list_search_key
{
    method_addr_p method_addr;
    int method_addr_yes;
    bmi_size_t actual_size;
    int actual_size_yes;
    bmi_size_t expected_size;
    int expected_size_yes;
    bmi_msg_tag_t msg_tag;
    int msg_tag_yes;
    bmi_op_id_t op_id;
    int op_id_yes;
    bmi_flag_t mode_mask;
    int mode_mask_yes;
};

int op_list_count(op_list_p olp);
op_list_p op_list_new(void);
void op_list_add(op_list_p olp,
		 method_op_p oip);
void op_list_cleanup(op_list_p olp);
void op_list_remove(method_op_p oip);
void op_list_dump(op_list_p olp);
int op_list_empty(op_list_p olp);
method_op_p op_list_shownext(op_list_p olp);
method_op_p op_list_search(op_list_p olp,
			   struct op_list_search_key *key);
int op_list_search_array(int incount,
			 bmi_op_id_t * id_array,
			 int *outcount,
			 int *index_array,
			 bmi_error_code_t * error_code_array,
			 bmi_size_t * actual_size_array,
			 void **user_ptr_array,
			 op_list_p olp);

#endif /* __OP_LIST_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
