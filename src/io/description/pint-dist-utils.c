/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pvfs2-dist-simple-stripe.h"
#include "pint-dist-utils.h"

/* Default distributions */
extern PINT_dist basic_dist;
extern PINT_dist simple_stripe_dist;

/* Struct for determining how to set a distribution parameter by name */
typedef struct PINT_dist_param_offset_s
{
    char* dist_name;
    char* param_name;
    size_t offset;
    size_t size;
} PINT_dist_param_offset;

/* Dist param table */
static PINT_dist_param_offset* PINT_dist_param_table = 0;
static size_t PINT_dist_param_table_size = 0;

/* Return a pointer to the dist param offset for this parameter, or null
 *  if none exists
 */
static PINT_dist_param_offset* PINT_get_param_offset(const char* dist_name,
                                                     const char* param_name)
{
    PINT_dist_param_offset* dpo = 0;
    if (0 != PINT_dist_param_table)
    {
        int i;
        for (i = 0; i < PINT_dist_param_table_size; i++)
        {
            dpo = PINT_dist_param_table + i;
            if (0 == strcmp(dpo->dist_name, dist_name) &&
                0 == strcmp(dpo->param_name, param_name))
            {
                return dpo;
            }
        }
    }
    return NULL;
}

/* PINT_dist_initialize implementation */
int PINT_dist_initialize(void)
{
    /* Register the basic distribution */
    PINT_register_distribution(&basic_dist);    
    
    /* Register the simple stripe distribution */
    PINT_register_distribution(&simple_stripe_dist);    
    
    return 0;
}

/* PINT_dist_finalize implementation */
void PINT_dist_finalize(void)
{
    /* Nothing yet */
}

/*  PINT_dist_default_get_num_dfiles implementation */
int PINT_dist_default_get_num_dfiles(void* params,
                                     uint32_t num_servers_requested,
                                     uint32_t num_dfiles_requested)
{
    int dfiles;
    if (0 < num_dfiles_requested)
    {
        dfiles = num_dfiles_requested;
    }
    else
    {
        dfiles = num_servers_requested;
    }
    return dfiles;
}

/*  PINT_dist_default_set_param implementation */
int PINT_dist_default_set_param(const char* dist_name, void* params,
                                const char* param_name, void* value)
{
    int rc = 0;
    PINT_dist_param_offset* offset_data;
    offset_data = PINT_get_param_offset(dist_name, param_name);
    if (0 != offset_data)
    {
        memcpy(params + offset_data->offset, value, offset_data->size);
    }
    else
    {
        rc = -1;
    }
    return rc;
}

int PINT_dist_register_param_offset(const char* dist_name,
                                    const char* param_name,
                                    size_t offset,
                                    size_t field_size)
{
    /*fprintf(stderr, "Attempting to reg: %s %s %u %u\n",
            dist_name, param_name, offset, field_size);
    */
    return 0;
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
