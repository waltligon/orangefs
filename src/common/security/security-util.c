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


static const PVFS_capability null_capability = {0};


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
    if (!src || !dest || (src == dest))
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

const PVFS_capability *PINT_null_capability(void)
{
    return &null_capability;
}

int PINT_capability_is_null(const PVFS_capability *cap)
{
    return (memcmp(cap, &null_capability, sizeof(PVFS_capability)) == 0);
}

void PINT_cleanup_credential(PVFS_credential *cred)
{
    if (cred)
    {
        free(cred->group_array);
        free(cred->issuer_id);
        free(cred->signature);
    }
}

void PINT_release_credential(PVFS_credential *cred)
{
    PINT_cleanup_credential(cred);
    free(cred);
}

/* TODO: fix for the no security case. the previous assumption that
 * credentials are always signed will probably no longer hold.
 */
int PINT_copy_credential(const PVFS_credential *src, PVFS_credential *dest)
{
    if (!src || !dest || (src == dest))
    {
        return -PVFS_EINVAL;
    }

    memcpy(dest, src, sizeof(PVFS_credential));
    dest->group_array = NULL;
    dest->issuer_id = NULL;
    dest->signature = NULL;

    if (src->num_groups)
    {
        dest->group_array = calloc(src->num_groups, sizeof(PVFS_gid));
        if (!dest->group_array)
        {
            return -PVFS_ENOMEM;
        }
        memcpy(dest->group_array, src->group_array,
               src->num_groups * sizeof(PVFS_gid));
    }

    dest->issuer_id = strdup(src->issuer_id);
    if (!dest->issuer_id)
    {
        free(dest->group_array);
        return -PVFS_ENOMEM;
    }

    dest->signature = calloc(src->sig_size, 1);
    if (!dest->signature)
    {
        free(dest->issuer_id);
        free(dest->group_array);
        return -PVFS_ENOMEM;
    }
    memcpy(dest->signature, src->signature, src->sig_size);

    return 0;
}

PVFS_credential *PINT_dup_credential(const PVFS_credential *cred)
{
    PVFS_credential *newcred;
    int ret;

    if (!cred)
    {
        return NULL;
    }

    newcred = (PVFS_credential*)malloc(sizeof(PVFS_credential));
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


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

