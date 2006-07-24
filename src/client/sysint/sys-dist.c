/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <string.h>
#include "gossip.h"
#include "pint-distribution.h"
#include "pvfs2-sysint.h"

/** Return the distribution associated with the given identifier, or null
 *  if none exists.
 */
PVFS_sys_dist* PVFS_sys_dist_lookup(const char* dist_name)
{
    PVFS_sys_dist* sys_dist = 0;

    if (0 != dist_name)
    {
        /* Construct a dummy dist to lookup the registered dist */
        PINT_dist template_dist;
        template_dist.dist_name = (char*)dist_name;
        template_dist.params = 0;
        template_dist.methods = 0;

        if (0 == PINT_dist_lookup(&template_dist))
        {
            /* Copy the data from the registered dist */
            sys_dist = malloc(sizeof(PVFS_sys_dist));
            if (0 != sys_dist)
            {
                sys_dist->name = malloc(template_dist.name_size);
                sys_dist->params = malloc(template_dist.param_size);
                if (0 != sys_dist->name && 0 != sys_dist->params)
                {
                    memcpy(sys_dist->name, template_dist.dist_name,
                           template_dist.name_size);
                    memcpy(sys_dist->params, template_dist.params,
                           template_dist.param_size);
                }
                else
                {
                    free(sys_dist->name);
                    free(sys_dist->params);
                    free(sys_dist);
                    sys_dist = 0;
                }
            }
        }
    }
    return sys_dist;
}

/** Free resources associated with this distribution.
 */
PVFS_error PVFS_sys_dist_free(PVFS_sys_dist* dist)
{
    if (0 != dist)
    {
        free(dist->name);
        free(dist->params);
        free(dist);
    }
    return 0;
}

/** Set the named distribution parameter with the given value.
 */
PVFS_error PVFS_sys_dist_setparam(
    PVFS_sys_dist* dist,
    const char* param,
    void* value)
{
    PVFS_error rc = -PVFS_EINVAL;
    if (0 != dist)
    {
        /* Construct a dummy dist to lookup the registered dist */
        PINT_dist template_dist;
        template_dist.dist_name = dist->name;
        template_dist.params = 0;
        template_dist.methods = 0;

        if (0 == PINT_dist_lookup(&template_dist))
        {
            rc = template_dist.methods->set_param(dist->name,
                                                  dist->params,
                                                  param, value);
            if (0 != rc)
            {
                rc = -PVFS_EINVAL;
                gossip_err("Error: Distribution does not have parameter: %s\n",
                           param);
            }
        }
    }
    return rc;
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
