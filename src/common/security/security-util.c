/* 
 * (C) 2008 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <string.h>

#include "pvfs2-config.h"
#include "security-types.h"
#include "security-util.h"


/*  PINT_dup_capability
 *
 *  When passed a valid capability pointer this function will duplicate
 *  it and return the copy.  User must make sure to free both the new and
 *  old capabilities as normal.
 *	
 *  returns NULL on error
 *  returns valid PVFS_capability * on success
 */
PVFS_capability *PINT_dup_capability(const PVFS_capability *cap)
{
    PVFS_capability *newcap;
    int ret;
    
    if (!cap)
    {
        return NULL;
    }

    newcap = (PVFS_capability*)malloc(sizeof(PVFS_capability));
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

int PINT_copy_capability(const PVFS_capability *src, PVFS_capability *dest)
{
    if (!src || !dest)
    {
        return -PVFS_EINVAL;
    }

    memcpy(dest, src, sizeof(PVFS_capability));
    dest->signature = NULL;
    dest->handle_array = NULL;

#ifndef SECURITY_ENCRYPTION_NONE
    dest->signature = (unsigned char*)malloc(src->sig_size);
    if (!dest->signature)
    {
        return -PVFS_ENOMEM;
    }
    memcpy(dest->signature, src->signature, src->sig_size);
#endif /* SECURITY_ENCRYPTION_NONE */

    if (src->num_handles)
    {
        dest->handle_array = calloc(src->num_handles, sizeof(PVFS_handle));
        if (!dest->handle_array)
        {
            free(dest->signature);
            return -PVFS_ENOMEM;
        }
        memcpy(dest->handle_array, src->handle_array, 
               src->num_handles * sizeof(PVFS_handle));
    }

    return 0;
}

/*  PINT_release_capability
 *
 *  Frees any memory associated with a capability structure.
 *	
 *  no return value
 */
void PINT_release_capability(PVFS_capability *cap)
{
    if (cap)
    {
    	free(cap->signature);
    	free(cap->handle_array);
    	free(cap);
    }
}

void PINT_release_credential(PVFS_credential *cred)
{
    if (cred)
    {
        free(cred->group_array);
        free(cred->issuer_id);
        free(cred->signature);
        free(cred);
    }
}

PVFS_credential *PINT_dup_credential(const PVFS_credential *cred)
{
    PVFS_credential *ret = NULL;

    if (!cred)
    {
        return NULL;
    }

    ret = (PVFS_credential*)malloc(sizeof(PVFS_credential));
    if (!ret)
    {
        return NULL;
    }

    memcpy(ret, cred, sizeof(PVFS_credential));
    ret->group_array = NULL;
    ret->issuer_id = NULL;
    ret->signature = NULL;

    if (cred->num_groups)
    {
        ret->group_array = calloc(cred->num_groups, sizeof(PVFS_gid));
        if (!ret->group_array)
        {
            free(ret);
            return NULL;
        }
        memcpy(ret->group_array, cred->group_array, 
               cred->num_groups * sizeof(PVFS_gid));
    }

    ret->issuer_id = strdup(cred->issuer_id);
    if (!ret->issuer_id)
    {
        free(ret->group_array);
        free(ret);
        return NULL;
    }

    ret->signature = (unsigned char*)calloc(cred->sig_size, 1);
    if (!ret->signature)
    {
        free(ret->issuer_id);
        free(ret->group_array);
        free(ret);
        return NULL;
    }
    memcpy(ret->signature, cred->signature, cred->sig_size);

    return ret;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

