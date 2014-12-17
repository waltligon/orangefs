/* 
 * (C) 2003 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include <stdlib.h>
#include <string.h>
#include "gossip.h"
#include "pvfs2-internal.h"
#include "pint-distribution.h"
#include "pvfs2-sysint.h"
#include "token-utils.h"
#include "pint-hint.h"

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

PVFS_error PVFS_dist_pv_pairs_extract_and_add(const char * pv_pairs,
                                      void * dist)
{
    PVFS_error rc = -PVFS_EINVAL;
    unsigned int count = 0;
    char copy[PLUS_ONE(PVFS_HINT_PARAM_VAL_PAIRS_MAX_STRLEN)] = {0};
    rc = iterate_tokens_inplace(pv_pairs,
                                  PVFS_HINT_PARAM_VAL_PAIRS_SEP,
                                  copy,
                                  PVFS_HINT_PARAM_VAL_PAIRS_MAX_STRLEN,
                                  PVFS_HINT_PARAM_VAL_PAIRS_MAX,
                                  &count,
                                  &PVFS_dist_pv_pair_split,
                                  dist,
                                  NULL,
                                  NULL);
    if(rc != 0)
    {
        rc = -PVFS_EINVAL;
        gossip_err("Error: There was a problem parsing your distribution "
                   "parameter/value pairs: %s\n",
                   pv_pairs);
    }
    return rc;
}

PVFS_error PVFS_dist_pv_pair_split(const char * pv_pair, void *dist)
{
    PVFS_error rc = -PVFS_EINVAL;
    PVFS_sys_dist *dist_p = 0;
    unsigned int count = 0;
    char copy[PLUS_ONE(PVFS_HINT_PARAM_VAL_PAIR_MAX_STRLEN)] = {0};
    char copy_out_param_name[PLUS_ONE(PVFS_HINT_MAX_NAME_LENGTH)] = {0};
    char copy_out_param_value[PLUS_ONE(PVFS_HINT_MAX_LENGTH)] = {0};
    char * copy_out[] = {copy_out_param_name, copy_out_param_value};
    unsigned int copy_out_token_lengths[] = {PVFS_HINT_MAX_NAME_LENGTH,
                                             PVFS_HINT_MAX_LENGTH};

    if(dist)
    {
        dist_p = (PVFS_sys_dist *) dist;
    }

    rc = iterate_tokens_inplace(pv_pair,
                           PVFS_HINT_PARAM_VAL_SEP,
                           copy,
                           PVFS_HINT_PARAM_VAL_PAIR_MAX_STRLEN,
                           2,
                           &count,
                           NULL,
                           NULL,
                           copy_out,
                           copy_out_token_lengths
                          );
    if(rc != 0)
    {
        gossip_err("Error: There was a problem parsing your supplied "
                   "distribution parameter/value pair: %s\n",
                    pv_pair);
        return -PVFS_EINVAL;
    }

    if(count == 2)
    {
#if 0
        printf("dist param name: %s\n"
               "dist param value: %s\n",
               copy_out_param_name,
               copy_out_param_value);
#endif 

        /* TODO This is currently hard-coded to look for strip_size ONLY rather
         *  than a better approach at the moment."
         * We should check the expected p:v pair based on the distribution
         *  name. */
        if(strcmp(copy_out_param_name, "strip_size") == 0)
        {
            long long int strip_size = strtoll(copy_out_param_value,
                                               NULL,
                                               10);
#if 0
            printf("lld representation of strip_size=%lld\n",
                   strip_size);
#endif
            rc = PVFS_sys_dist_setparam(dist_p,
                                   "strip_size",
                                   &strip_size);
            if(rc != 0)
            {
                gossip_err("Error: There was a problem setting the parsed "
                           "distribution parameter value in the dist "
                           "structure:\n"
                           "\tparameter_name=%s\n"
                           "\tparameter_value=%s\n",
                           copy_out_param_name,
                           copy_out_param_value);
                return -PVFS_EINVAL;
            }
        }
        return 0;
    }
    return -PVFS_EINVAL;
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
