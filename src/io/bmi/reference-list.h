/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/*
 * Header file for reference list management functions.  Reference structures
 * are used to maintain the mapping between BMI_addr_t and
 * method_addr_p addresses.   
 */

#ifndef __REFERENCE_LIST_H
#define __REFERENCE_LIST_H

#include "bmi-types.h"
#include "bmi-method-support.h"
#include "quicklist.h"
#include "quickhash.h"

typedef struct qlist_head *ref_list_p;

/**********************************************************************/
/* this is the basic reference structure for the glue layer above the
 * actual methods.  It keeps up with the bmi_addr, id string, method
 * address, and pointers to each of the appropriate method functions.
 */
struct ref_st
{
    BMI_addr_t bmi_addr;	/* the identifier passed out of the BMI layer */
    char *id_string;		/* the id string that represents this reference */
    bmi_method_addr_p method_addr;	/* address structure used by the method */

    /* pointer to the appropriate method interface */
    struct bmi_method_ops *interface;

    /* linked list entry */
    struct qlist_head list_link;
    int ref_count;
    struct qhash_head hash_link;
};

typedef struct ref_st ref_st, *ref_st_p;

/********************************************************************
 * reference list management prototypes
 */

ref_list_p ref_list_new(void);
void ref_list_add(ref_list_p rlp,
		  ref_st_p rsp);
ref_st_p ref_list_search_addr(ref_list_p rlp,
			      BMI_addr_t my_addr);
ref_st_p ref_list_rem(ref_list_p rlp,
		      BMI_addr_t my_addr);
ref_st_p ref_list_search_method_addr(ref_list_p rlp,
				     bmi_method_addr_p map);
ref_st_p ref_list_search_str(ref_list_p rlp,
			     const char *idstring);
void ref_list_cleanup(ref_list_p rlp);
ref_st_p alloc_ref_st(void);
void dealloc_ref_st(ref_st_p deadref);


#endif /* __REFERENCE_LIST_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
