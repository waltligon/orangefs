/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


/*
 * reference_list.c - functions to handle the creation and modification of 
 * reference structures for the BMI layer
 *
 * This is built on top of the llist.[ch] files
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <reference-list.h>
#include <gossip.h>
#include <id-generator.h>

/***************************************************************
 * Function prototypes
 */

static int ref_list_cmp_addr(void *key, void *refp);
static int ref_list_cmp_method_addr(void *key, void *refp);
static int ref_list_cmp_str(void *key, void *refp);
static void ref_st_free(void* rsp);


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
	return(llist_new());
}

/*
 * ref_list_add()
 *
 * adds a reference to the list.  
 *
 * returns 0 on success, -1 on failure
 */
int ref_list_add(ref_list_p rlp, ref_st_p rsp)
{
	return (llist_add(rlp, (void *) rsp));
}

/*
 * ref_list_search_addr()
 *
 * looks for a reference structure in the list that matches the given
 * bmi_addr_t.
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_search_addr(ref_list_p rlp, bmi_addr_t my_addr)
{
	return((ref_st_p) llist_search(rlp, (void *) (&my_addr), ref_list_cmp_addr));
}


/*
 * ref_list_search_method_addr()
 *
 * looks for a reference structure in the list that matches the given
 * method_addr_p.
 *
 * returns a pointer to the structure on success, NULL on failure.
 */
ref_st_p ref_list_search_method_addr(ref_list_p rlp, method_addr_p map){
	
	return((ref_st_p) llist_search(rlp, (void*) (map), ref_list_cmp_method_addr));
}

/*
 * ref_list_search_str()
 *
 * looks for a reference structure in the list that matches the given
 * id string.
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_search_str(ref_list_p rlp, const char* idstring)
{
	return((ref_st_p) llist_search(rlp, (void *) (idstring), ref_list_cmp_str));
}

/*
 * ref_list_rem()
 *
 * removes the first match from the list - does not destroy it 
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_rem(ref_list_p rlp, bmi_addr_t my_addr)
{
	return((ref_st_p) llist_rem(rlp, (void *) (&my_addr), ref_list_cmp_addr));
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
	llist_free(rlp, ref_st_free);
}

/*
 * alloc_ref_st()
 *
 * allocates storage for a reference struct.
 *
 * returns a pointer to the new structure on success, NULL on failure.
 */
ref_st_p alloc_ref_st(void){

	int ssize = sizeof(struct ref_st);
	ref_st_p new_ref = NULL;
	int ret = -1;

	new_ref = (ref_st_p)malloc(ssize);
	if(!new_ref){
		return(NULL);
	}
	
	memset(new_ref, 0, ssize);

	/* we can go ahead and set the bmi_addr here */
	ret = id_gen_fast_register(&(new_ref->bmi_addr), new_ref);
	if(ret < 0)
	{
		dealloc_ref_st(new_ref);
		return(NULL);
	}

	return(new_ref);
}

/*
 * dealloc_ref_st()
 *
 * frees all memory associated with a reference structure
 * NOTE: it *does not*, however, destroy the associated method address.
 *
 * returns 0 on success, -1 on failure
 */
void dealloc_ref_st(ref_st_p deadref){

	if(!deadref){
		return;
	}

	if(deadref->id_string){
		free(deadref->id_string);
	}

	if(deadref->method_addr){
		deadref->interface->BMI_meth_set_info(BMI_DROP_ADDR, deadref->method_addr);
	}

	free(deadref);
}                                   

/****************************************************************
 * Internal utility functions
 */

/*
 * ref_list_cmp_addr()
 *
 * compares a given reference structure against a bmi_addr_t  to see
 * if it is a match.
 * 
 * returns a 0 if there it is a match, 1 otherwise.
 */
static int ref_list_cmp_addr(void *key, void *refp)
{
	if(((ref_st_p)refp)->bmi_addr == (*(bmi_addr_t *)key)){
		return(0);
	}
	return(1);

}

/*
 * ref_list_cmp_method_addr()
 *
 * compares a given reference structure against a method_addr_p pointer to
 * see if there is a match.
 *
 * returns a 0 if there is a match, 1 otherwise.
 */
static int ref_list_cmp_method_addr(void *key, void *refp){

	if(((ref_st_p)refp)->method_addr == (method_addr_p)key){
		return(0);
	}
	return(1);
}

/*
 * ref_list_cmp_str()
 *
 * compares a given reference structure against an id string to see
 * if it is a match.
 * 
 * returns a 0 if there it is a match, 1 otherwise.
 */
static int ref_list_cmp_str(void *key, void *refp)
{
	if(!strcmp(((ref_st_p)refp)->id_string, (char*)key)){
		return(0);
	}
	return(1);

}

/*
 * ref_st_free()
 *
 * used by ref_list_cleanup to free up the memory associated with a list
 * item.
 *
 * no return value
 */
static void ref_st_free(void* rsp){
	dealloc_ref_st((ref_st_p)rsp);
}
