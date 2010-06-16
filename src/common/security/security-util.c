/* 
 * (C) 2009 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <string.h>

#include "pvfs2-config.h"
#include "pvfs2-types.h"
#include "security-util.h"


/* PINT_null_capability
 *
 * Creates a capability object with no permissions.
 */
void PINT_null_capability(PVFS_capability *cap)
{
    memset(cap, 0, sizeof(PVFS_capability));
    cap->issuer = strdup("");
}

/* PINT_capability_is_null
 *
 * Checks for a null capability with no permissions.
 *
 * returns 1 if the capability is null
 * returns 0 if the capability is not null
 */
int PINT_capability_is_null(const PVFS_capability *cap)
{
    int ret;

    ret = (!strcmp(cap->issuer, "")) && (cap->op_mask == 0);

    return ret;
}

/* PINT_dup_capability
 *
 * Duplicates a capability object by allocating memory for the
 * new object and then performing a deep copy.
 *
 * returns the new capability object on success
 * returns NULL on error
 */
PVFS_capability *PINT_dup_capability(const PVFS_capability *cap)
{
    PVFS_capability *newcap;
    int ret;
    
    if (!cap)
    {
        return NULL;
    }

    newcap = malloc(sizeof(PVFS_capability));
    if (!newcap)
    {
        return NULL;
    }

    ret = PINT_copy_capability(cap, newcap);
    if (ret < 0)
    {
        free(newcap);
        newcap = NULL;
    }

    return newcap;
}

/* PINT_copy_capability
 *
 * Performs a deep copy of a capability object.
 *
 * returns 0 on success
 * returns negative PVFS error code on failure
 */
int PINT_copy_capability(const PVFS_capability *src, PVFS_capability *dest)
{
    if (!src || !dest || (src == dest))
    {
        return -PVFS_EINVAL;
    }

    /* first copy by value */
    memcpy(dest, src, sizeof(PVFS_capability));
    dest->issuer = NULL;
    dest->signature = NULL;
    dest->handle_array = NULL;

    dest->issuer = strdup(src->issuer);
    if (!dest->issuer)
    {
	return -PVFS_ENOMEM;
    }

    if (src->sig_size)
    {
        dest->signature = malloc(src->sig_size);
        if (!dest->signature)
        {
            free(dest->issuer);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->signature, src->signature, src->sig_size);
    }

    if (src->num_handles)
    {
	dest->handle_array = calloc(src->num_handles, sizeof(PVFS_handle));
	if (!dest->handle_array)
	{
	    free(dest->signature);
	    free(dest->issuer);
	    return -PVFS_ENOMEM;
	}
	memcpy(dest->handle_array, src->handle_array,
	       src->num_handles * sizeof(PVFS_handle));
    }

    return 0;
}

/* PINT_cleanup_capability
 *
 * Destructs a capability object by freeing its internal structures.
 * After this function returns the capability object is in an
 * invalid state.
 */
void PINT_cleanup_capability(PVFS_capability *cap)
{
    if (cap)
    {
    	free(cap->handle_array);
	free(cap->signature);
	free(cap->issuer);

        cap->handle_array = NULL;
        cap->signature = NULL;
        cap->sig_size = 0;
        cap->issuer = NULL;
    }
}

/* PINT_dup_credential
 *
 * Duplicates a credential object by allocating memory for the
 * new object and then performing a deep copy.
 *
 * returns the new credential object on success
 * returns NULL on error
 */
PVFS_credential *PINT_dup_credential(const PVFS_credential *cred)
{
    PVFS_credential *newcred;
    int ret;

    if (!cred)
    {
        return NULL;
    }

    newcred = malloc(sizeof(PVFS_credential));
    if (!newcred)
    {
        return NULL;
    }

    ret = PINT_copy_credential(cred, newcred);
    if (ret < 0)
    {
        free(newcred);
        return NULL;
    }

    return newcred;
}

/* PINT_copy_credential
 *
 * Performs a deep copy of a credential object.
 *
 * returns 0 on success
 * returns negative PVFS error code on failure
 */
int PINT_copy_credential(const PVFS_credential *src, PVFS_credential *dest)
{
    if (!src || !dest || (src == dest))
    {
        return -PVFS_EINVAL;
    }

    /* first copy by value */
    memcpy(dest, src, sizeof(PVFS_credential));
    dest->issuer = NULL;
    dest->signature = NULL;
    dest->group_array = NULL;

    dest->issuer = strdup(src->issuer);
    if (!dest->issuer)
    {
        return -PVFS_ENOMEM;
    }

    if (src->sig_size)
    {
        dest->signature = malloc(src->sig_size);
        if (!dest->signature)
        {
            free(dest->issuer);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->signature, src->signature, src->sig_size);
    }

    if (src->num_groups)
    {
        dest->group_array = calloc(src->num_groups, sizeof(PVFS_gid));
        if (!dest->group_array)
        {
            free(dest->signature);
            free(dest->issuer);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->group_array, src->group_array,
               src->num_groups * sizeof(PVFS_gid));
    }
    
    return 0;
}

/* PINT_cleanup_credential
 *
 * Destructs a credential object by freeing its internal structures.
 * After this function returns the credential object is in an
 * invalid state.
 */
void PINT_cleanup_credential(PVFS_credential *cred)
{
    if (cred)
    {
        free(cred->group_array);
        free(cred->issuer);
        free(cred->signature);

        cred->group_array = NULL;
        cred->issuer = NULL;
        cred->signature = NULL;
        cred->sig_size = 0;
    }
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
