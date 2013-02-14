/*
 * (C) 2010-2012 Clemson University and Omnibond LLC
 *
 * See COPYING in top-level directory.
 *
 * Windows credential functions
 *
 */

#include <stdlib.h>
#include <time.h>

#include "pint-util.h"

#include "cred.h"

/* initialize credential - credential must be allocated */ 
int init_credential(PVFS_credential *cred)
{
    if (cred == NULL)
    {
        return -PVFS_EINVAL;
    }

    memset(cred, 0, sizeof(PVFS_credential));

    /* blank issuer */
    cred->issuer = strdup("");
    if (!cred->issuer)
    {
        return -PVFS_ENOMEM;
    }

    return 0;
}

/* free credential fields - caller must free credential */
void cleanup_credential(PVFS_credential *cred)
{
    if (cred) 
    {
        if (cred->group_array)
        {
            free(cred->group_array);
            cred->group_array = NULL;
        }
        if (cred->signature)
        {
            free(cred->signature);
            cred->signature = NULL;
        }
        if (cred->issuer)
        {
            free(cred->issuer);
            cred->issuer = NULL;
        }

        cred->num_groups = 0;
        cred->sig_size = 0;
    }
}

/* check if credential is a member of group */
int credential_in_group(PVFS_credential *cred, PVFS_gid group)
{
    unsigned int i;

    for (i = 0; i < cred->num_groups; i++)
    {
        if (cred->group_array[i] == group)
        {
            return 1;
        }
    }

    return 0;
}

/* add group to credential group list */
void credential_add_group(PVFS_credential *cred, PVFS_gid group)
{
    PVFS_gid *group_array;
    unsigned int i;

    if (cred->num_groups > 0)
    {
        /* copy existing group array and append group */
        cred->num_groups++;
        group_array = (PVFS_gid *) malloc(sizeof(PVFS_gid) * cred->num_groups);
        for (i = 0; i < cred->num_groups - 1; i++)
        {
            group_array[i] = cred->group_array[i];
        }
        group_array[cred->num_groups-1] = group;

        free(cred->group_array);
        cred->group_array = group_array;
    }
    else
    {
        /* add one group */
        cred->group_array = (PVFS_gid *) malloc(sizeof(PVFS_gid));
        cred->group_array[0] = group;
        cred->num_groups = 1;
    }
}

/* set the credential timeout to current time + timeout
   use PVFS2_DEFAULT_CREDENTIAL_TIMEOUT */
void credential_set_timeout(PVFS_credential *cred, PVFS_time timeout)
{
    cred->timeout = PINT_util_get_current_time() + timeout;
}