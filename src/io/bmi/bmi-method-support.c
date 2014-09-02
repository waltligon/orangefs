/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* These are some support functions shared across the
 * various BMI methods.
 */

#include <string.h>

#include "pvfs2-internal.h"
#include "bmi-types.h"
#include "bmi-method-support.h"
#include "id-generator.h"
#include "reference-list.h"
#include "gossip.h"


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
 * Adding protocol sub-zones - networks using a given protocol that are
 * not connected, thus tending to behave like distinct protocols running
 * the same methods.  string addr is now:
 *
 * <protocol>[-<subzone>]://<machine>:<port>
 *
 * A subzone can be any string not including a colon.  In this function,
 * the key will be only the protocol (as it was before) but it will
 * tolerate the presense of the subzone.
 *
 * Phil: Boy, I sure do hate writing code to parse strings...
 * 
 * returns a pointer to the new string containg everything after ://
 * on success, NULL on failure.
 */
char *string_key(const char *key,
		 const char *id_string)
{
    char *retstr = NULL;
    char *tmpstr = NULL;
    int klen = 0;
    int plen = 0;
    int rlen = 0;

    if ((!id_string) || (!key))
    {
        /* error */
	return NULL;
    }

    klen = strlen(key);

    while(*id_string && (tmpstr = strstr(id_string, key)))
    {
        int len, len2;

        /* first verify the key matches the protocol */
        len = strcspn(tmpstr, "-:");
        if (len != klen)
        {
            /* protocol was not found */
	    goto keepsearching;
        }
        plen = len;
        
        /* skip the zone if there is one */

        /* first check for and eat the dash */
        if (*(tmpstr + plen) == '-')
        {
            /* a zone is present */
            plen++; /* skip over dash */
            /* make sure zone looks valid */
            len2 = strcspn(tmpstr + plen, ":-/{}\n\t@$#&*^%! ;,.?");
            len  = strcspn(tmpstr + plen, ":");
            if (len != len2)
            {
                /* malformed zone string */
	        goto keepsearching;
            }
            plen += len;
        }

        /* check for colon */
        if (*(tmpstr + plen) != ':')
        {
            /* malformed string */
	    goto keepsearching;
        }
        plen++; /* skip over colon */

        /* eat the slashes */
        len = strspn(tmpstr + plen, "/");
        if (len != 2)
        {
            /* malformed string */
	    goto keepsearching;
        }
        plen += len;

        /* we found it jump out of the loop */
        break;  

keepsearching:
        /* continue searching just past last substr we found */
        id_string = tmpstr + 1;
        tmpstr = NULL;
    }

    if (!tmpstr)
    {
        /* returned NULL so not found */
        return NULL;
    }

    /* tmpstr + plen now points just past :// */

    /* find end of string, not including white space */
    rlen = strcspn(tmpstr + plen, ", \t\n");

    /* malloc the string, copy it in and return it */
    retstr = (char *)malloc(rlen + 1);
    if (!retstr)
    {
        /* out of memory */
        return NULL;
    }
    memcpy(retstr, tmpstr + plen, rlen);
    retstr[rlen] = '\0';

    return (retstr);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
