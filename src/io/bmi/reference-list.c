/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/*
 * reference_list.c - functions to handle the creation and modification of 
 * reference structures for the BMI layer
 *
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "reference-list.h"
#include "gossip.h"
#include "id-generator.h"


/***************************************************************
 * Visible functions
 */

/*
 * ref_list_new()
 *
 * creates a new reference list.
 *
 * returns pointer to an empty list or NULL on failure.
 */
ref_list_p ref_list_new(void)
{

    ref_list_p tmp_list = NULL;

    tmp_list = (ref_list_p) malloc(sizeof(struct qlist_head));
    if (tmp_list)
    {
	INIT_QLIST_HEAD(tmp_list);
    }
    return (tmp_list);
}

/*
 * ref_list_add()
 *
 * adds a reference to the list.  
 *
 * no return value
 */
void ref_list_add(ref_list_p rlp,
		  ref_st_p rsp)
{
    qlist_add(&(rsp->list_link), rlp);
}

/*
 * ref_list_search_addr()
 *
 * looks for a reference structure in the list that matches the given
 * bmi_addr_t.
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_search_addr(ref_list_p rlp,
			      bmi_addr_t my_addr)
{
    ref_list_p tmp_link = NULL;
    ref_st_p tmp_entry = NULL;

    qlist_for_each(tmp_link, rlp)
    {
	tmp_entry = qlist_entry(tmp_link, struct ref_st,
				list_link);
	if (tmp_entry->bmi_addr == my_addr)
	    return (tmp_entry);
    }
    return (NULL);
}


/*
 * ref_list_search_method_addr()
 *
 * looks for a reference structure in the list that matches the given
 * method_addr_p.
 *
 * returns a pointer to the structure on success, NULL on failure.
 */
ref_st_p ref_list_search_method_addr(ref_list_p rlp,
				     method_addr_p map)
{
    ref_list_p tmp_link = NULL;
    ref_st_p tmp_entry = NULL;

    qlist_for_each(tmp_link, rlp)
    {
	tmp_entry = qlist_entry(tmp_link, struct ref_st, list_link);
	if (tmp_entry->method_addr == map)
	    return (tmp_entry);
    }
    return (NULL);
}

/*
 * ref_list_search_str()
 *
 * looks for a reference structure in the list that matches the given
 * id string.
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_search_str(ref_list_p rlp,
			     const char *idstring)
{
    ref_list_p tmp_link = NULL;
    ref_st_p tmp_entry = NULL;

    qlist_for_each(tmp_link, rlp)
    {
	tmp_entry = qlist_entry(tmp_link, struct ref_st,
				list_link);
	if (!strcmp(tmp_entry->id_string, idstring))
	    return (tmp_entry);
    }
    return (NULL);
}

/*
 * ref_list_rem()
 *
 * removes the first match from the list - does not destroy it 
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_rem(ref_list_p rlp,
		      bmi_addr_t my_addr)
{
    ref_list_p tmp_link = NULL;
    ref_list_p scratch = NULL;
    ref_st_p tmp_entry = NULL;

    qlist_for_each_safe(tmp_link, scratch, rlp)
    {
	tmp_entry = qlist_entry(tmp_link, struct ref_st, list_link);
	if (tmp_entry->bmi_addr == my_addr)
	{
	    qlist_del(&tmp_entry->list_link);
	    return (tmp_entry);
	}
    }
    return (NULL);
}



/*
 * ref_list_cleanup()
 *
 * frees up the list and all data associated with it.
 *
 * no return values
 */
void ref_list_cleanup(ref_list_p rlp)
{
    ref_list_p tmp_link = NULL;
    ref_list_p scratch = NULL;
    ref_st_p tmp_entry = NULL;

    qlist_for_each_safe(tmp_link, scratch, rlp)
    {
	tmp_entry = qlist_entry(tmp_link, struct ref_st,
				list_link);
	free(tmp_entry);
    }

    free(rlp);
    return;
}

/*
 * alloc_ref_st()
 *
 * allocates storage for a reference struct.
 *
 * returns a pointer to the new structure on success, NULL on failure.
 */
ref_st_p alloc_ref_st(void)
{

    int ssize = sizeof(struct ref_st);
    ref_st_p new_ref = NULL;
    int ret = -1;

    new_ref = (ref_st_p) malloc(ssize);
    if (!new_ref)
    {
	return (NULL);
    }

    memset(new_ref, 0, ssize);

    /* we can go ahead and set the bmi_addr here */
    ret = id_gen_fast_register(&(new_ref->bmi_addr), new_ref);
    if (ret < 0)
    {
	dealloc_ref_st(new_ref);
	return (NULL);
    }

    return (new_ref);
}

/*
 * dealloc_ref_st()
 *
 * frees all memory associated with a reference structure
 * NOTE: it *does not*, however, destroy the associated method address.
 *
 * returns 0 on success, -1 on failure
 */
void dealloc_ref_st(ref_st_p deadref)
{

    if (!deadref)
    {
	return;
    }

    if (deadref->id_string)
    {
	free(deadref->id_string);
    }

    if (deadref->method_addr)
    {
	deadref->interface->BMI_meth_set_info(BMI_DROP_ADDR,
					      deadref->method_addr);
    }

    free(deadref);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
