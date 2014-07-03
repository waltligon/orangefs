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
#include <assert.h>

#include "reference-list.h"
#include "gossip.h"
#include "id-generator.h"
#include "quickhash.h"

static struct qhash_table* str_table = NULL;
#define STR_TABLE_SIZE 137

/***************************************************************
 * Visible functions
 */

static int ref_list_compare_key_entry(void* key, struct qhash_head* link);

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

    /* There is currently never more than one reference list in BMI.  If we
     * ever have a need for more, then this hash table should be moved from
     * a static global to actually be part of the ref_list_p.
     */
    assert(str_table == NULL);

    str_table = qhash_init(
        ref_list_compare_key_entry,
        quickhash_string_hash, 
        STR_TABLE_SIZE);

    if(!str_table)
    {
        return(NULL);
    }

    tmp_list = (ref_list_p) malloc(sizeof(struct qlist_head));
    if(!tmp_list)
    {
        qhash_finalize(str_table);
        str_table = NULL;
        return(NULL);
    }

    INIT_QLIST_HEAD(tmp_list);
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
    if(rsp->id_string)
    {
        qhash_add(str_table, rsp->id_string, &rsp->hash_link);
    }

    qlist_add(&(rsp->list_link), rlp);
}

/*
 * ref_list_search_addr()
 *
 * looks for a reference structure in the list that matches the given
 * BMI_addr_t.
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_search_addr(ref_list_p rlp,
			      BMI_addr_t my_addr)
{
    return(id_gen_safe_lookup(my_addr));
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
				     bmi_method_addr_p map)
{
    return(map->parent);
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

    struct qhash_head* tmp_link;

    tmp_link = qhash_search(str_table, (char*)idstring);
    if(!tmp_link)
    {
        return(NULL);
    }

    return(qlist_entry(tmp_link, ref_st, hash_link));
}

/*
 * ref_list_rem()
 *
 * removes the first match from the list - does not destroy it 
 *
 * returns a pointer to the structure on success, a NULL on failure.
 */
ref_st_p ref_list_rem(ref_list_p rlp,
		      BMI_addr_t my_addr)
{
    ref_st_p tmp_entry;
    
    tmp_entry = id_gen_safe_lookup(my_addr);

    if(tmp_entry)
    {
        qlist_del(&tmp_entry->list_link);

        if(tmp_entry->id_string)
        {
            qhash_del(&tmp_entry->hash_link);
        }
    }
    return (tmp_entry);
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
        dealloc_ref_st(tmp_entry);
    }

    qhash_finalize(str_table);
    str_table = NULL;

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

    new_ref = (ref_st_p) malloc(ssize);
    if (!new_ref)
    {
	return (NULL);
    }

    memset(new_ref, 0, ssize);

    /* we can go ahead and set the bmi_addr here */
    id_gen_safe_register(&(new_ref->bmi_addr), new_ref);

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
	deadref->interface->set_info(BMI_DROP_ADDR, deadref->method_addr);
    }

    id_gen_safe_unregister(deadref->bmi_addr);

    free(deadref);
}

static int ref_list_compare_key_entry(void* key, struct qhash_head* link)
{
    char* key_string = (char*)key;
    ref_st_p tmp_entry = NULL;

    tmp_entry = qhash_entry(link, ref_st, hash_link);
    assert(tmp_entry);

    if(strcmp(tmp_entry->id_string, key_string) == 0)
    {
        return(1);
    }
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
