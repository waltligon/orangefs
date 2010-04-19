/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* These are some support functions shared across the
 * various BMI methods.
 */

#include <string.h>

#include "bmi-types.h"
#include "bmi-method-support.h"
#include "id-generator.h"
#include "reference-list.h"


/*
 * alloc_method_op()
 *
 * allocates storage for an operation info struct.
 *
 * returns a pointer to the new structure on success, NULL on failure.
 */
method_op_p bmi_alloc_method_op(bmi_size_t payload_size)
{

    /* we are going to allocate the full operation structure as a
     * contiguous region.  The method_data pointer is going to point to
     * the memory address immediately following the method_addr
     * structure.
     */
    method_op_p my_method_op = NULL;
    int ssize = sizeof(method_op_st);
    /* generic component */
    my_method_op = (method_op_p) malloc(ssize + payload_size);
    if (!my_method_op)
    {
	return (NULL);
    }
    memset(my_method_op, 0, (ssize + payload_size));

    id_gen_fast_register(&(my_method_op->op_id), my_method_op);

    my_method_op->error_code = 0;
    if (payload_size == 0)
    {
	my_method_op->method_data = NULL;
    }
    else
    {
	my_method_op->method_data = (char *) my_method_op + ssize;
    }

    return (my_method_op);
}

/* 
 * dealloc_method_op()
 *
 * frees the memory allocated to an method_op structure 
 * 
 * no return value
 */
void bmi_dealloc_method_op(method_op_p op_p)
{
    id_gen_fast_unregister(op_p->op_id);
    free(op_p);
    op_p = NULL;
    return;
}


/*
 * alloc_method_addr()
 * 
 * alloc_method_addr is used to generate a basic method_addr struct and
 * initialize it correctly.
 *
 * Returns a pointer to an allocated method_addr struct on success,
 * NULL on failure.
 */
struct bmi_method_addr *bmi_alloc_method_addr(int method_type,
				      bmi_size_t payload_size)
{

    /* we are going to allocate the full address structure as a
     * contiguous region.  The method_data pointer is going to point to
     * the memory address immediately following the method_addr
     * structure.
     */
    struct bmi_method_addr *my_method_addr = NULL;
    int ssize = sizeof(struct bmi_method_addr);
    /* generic component */
    my_method_addr = (struct bmi_method_addr *) malloc(ssize + payload_size);
    if (!my_method_addr)
    {
	return (NULL);
    }
    memset(my_method_addr, 0, (ssize + payload_size));
    my_method_addr->method_type = method_type;
    my_method_addr->ref_count = 1;

    my_method_addr->method_data = (char *) my_method_addr + ssize;

    return (my_method_addr);
}

/*
 * dealloc_method_addr()
 *
 * used to deallocate a method_addr structure safely.  mainly used by
 * list management functions.  MAKE SURE that any method specific
 * freeing has been handled before calling this function.  It is
 * oblivious to this.
 * 
 * no return value
 */
void bmi_dealloc_method_addr(bmi_method_addr_p my_method_addr)
{
    free(my_method_addr);
    my_method_addr = NULL;
    return;
}


/*
 * string_key()
 *
 * string_key is used to create a new string which is a subset of the
 * given id string.  The substring is a complete method address field
 * that begins right after <key>:// and ends with a null terminator.
 * The function strips all whitespace, commas, and address fields which do
 * not match the key.  
 *
 * Boy, I sure do hate writing code to parse strings...
 * 
 * returns a pointer to the new string on success, NULL on failure.
 */
char *string_key(const char *key,
		 const char *id_string)
{

    const char *holder = NULL;
    const char *end = NULL;
    char *newkey = NULL;
    char *retstring = NULL;
    int keysize = 0;
    int strsize = 0;
    int retsize = 0;

    if ((!id_string) || (!key))
    {
	return (NULL);
    }
    keysize = strlen(key);
    strsize = strlen(id_string);

    /* create a new key of the form <key>:// */
    if ((newkey = (char *) malloc(keysize + 4)) == NULL)
    {
	return (NULL);
    }
    strcpy(newkey, key);
    strcat(newkey, "://");

    holder = id_string;

    holder = strstr(holder, newkey);
    /* first match */
    if (holder)
    {
	end = strpbrk(holder, ", \t\n");
	if (end)
	{
	    end = end;	/* stop on terminator */
	}
	else
	{
	    end = id_string + strsize;	/* go to the end of the id string (\0) */
	}
	/* move holder so it doesn't include the opening key and deliminator */
	holder = holder + keysize + 3;
    }
    else
    {
	/* no match */
	free(newkey);
	return (NULL);
    }

    /* figure out how long our substring is */
    retsize = (end - holder);
    if ((retstring = (char *) malloc(retsize + 1)) == NULL)
    {
	free(newkey);
	return (NULL);
    }

    /* copy it out */
    strncpy(retstring, holder, retsize);
    retstring[retsize] = '\0';

    free(newkey);
    return (retstring);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
