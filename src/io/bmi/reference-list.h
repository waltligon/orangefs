/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/*
 * Header file for reference list management functions.  Reference structures
 * are used to maintain the mapping between bmi_addr_t and
 * method_addr_p addresses.   
 */

#ifndef __REFERENCE_LIST_H
#define __REFERENCE_LIST_H

#include <bmi-types.h>
#include <bmi-method-support.h>
#include <llist.h>

/**********************************************************************/
/* this is the basic reference structure for the glue layer above the
 * actual methods.  It keeps up with the bmi_addr, id string, method
 * address, and pointers to each of the appropriate method functions.
 */
struct ref_st{
	bmi_addr_t bmi_addr;    /* the identifier passed out of the BMI layer */
	char* id_string;			/* the id string that represents this reference */
	method_addr_p method_addr;	/* address structure used by the method */

	/* pointer to the appropriate method interface */
	struct bmi_method_ops* interface;
};

typedef llist_p ref_list_p;
typedef struct ref_st ref_st, *ref_st_p;

/********************************************************************
 * reference list management prototypes
 */

ref_list_p ref_list_new(void);
int ref_list_add(ref_list_p rlp, ref_st_p rsp);
ref_st_p ref_list_search_addr(ref_list_p rlp, bmi_addr_t my_addr);
ref_st_p ref_list_rem(ref_list_p rlp, bmi_addr_t my_addr);
ref_st_p ref_list_search_method_addr(ref_list_p rlp, method_addr_p map);
ref_st_p ref_list_search_str(ref_list_p rlp, const char* idstring);
void ref_list_cleanup(ref_list_p rlp);
ref_st_p alloc_ref_st(void);
void dealloc_ref_st(ref_st_p deadref);


#endif /* __REFERENCE_LIST_H */
