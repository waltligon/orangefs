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
    PVFS_capability *ret = NULL;
    
    if (!cap)
    {
        return NULL;
    }

    ret = (PVFS_capability*)malloc(sizeof(PVFS_capability));
    if (!ret)
    {
        return NULL;
    }
    memcpy(ret, cap, sizeof(PVFS_capability));
    ret->signature = NULL;
    ret->handle_array = NULL;

#ifndef SECURITY_ENCRYPTION_NONE
    ret->signature = (unsigned char*)malloc(cap->sig_size);
    if (!ret->signature)
    {
        free(ret);
        return NULL;
    }
    memcpy(ret->signature, cap->signature, cap->sig_size);
#endif /* SECURITY_ENCRYPTION_NONE */

    if (cap->num_handles)
    {
        ret->handle_array = calloc(cap->num_handles, sizeof(PVFS_handle));
        if (!ret->handle_array)
        {
            free(ret->signature);
            free(ret);
            return NULL;
        }
        memcpy(ret->handle_array, 
               cap->handle_array, 
               cap->num_handles * sizeof(PVFS_handle));
    }
    
    return ret;
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


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

