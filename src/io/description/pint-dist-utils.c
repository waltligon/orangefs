/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pvfs2-dist-simple-stripe.h"
#include "pvfs2-dist-varstrip.h"
#include "pvfs2-dist-twod-stripe.h"
#include "pint-dist-utils.h"

/* Default distributions */
extern PINT_dist basic_dist;
extern PINT_dist simple_stripe_dist;
extern PINT_dist varstrip_dist;
extern PINT_dist twod_stripe_dist;

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
static size_t PINT_dist_param_table_entries = 0;
static size_t PINT_dist_param_table_size = 0;
static int PINT_dist_param_table_alloc_inc = 10;

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
        for (i = 0; i < PINT_dist_param_table_entries; i++)
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
int PINT_dist_initialize(server_configuration_s* server_config)
{
    int ret = 0;
    
    /* Register the basic distribution */
    PINT_register_distribution(&basic_dist);    
    
    /* Register the varstrip distribution */
    PINT_register_distribution(&varstrip_dist);
    
    /* Register the simple stripe distribution */
    PINT_register_distribution(&simple_stripe_dist);    

    /* Register the twod stripe distribution */
    PINT_register_distribution(&twod_stripe_dist);

    return ret;
}

/* PINT_dist_finalize implementation */
void PINT_dist_finalize(void)
{
    int i;
    for (i = 0; i < PINT_dist_param_table_entries; i++)
    {
        free(PINT_dist_param_table[i].dist_name);
        free(PINT_dist_param_table[i].param_name);
    }
    free(PINT_dist_param_table);
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
        memcpy(((char *)params) + offset_data->offset, 
               value, offset_data->size);
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
    int dist_len, param_len;
    
    /* Increase the size of the param table if its full */
    if (PINT_dist_param_table_entries >= PINT_dist_param_table_size)
    {
        PINT_dist_param_offset* buf;
        int new_table_size = PINT_dist_param_table_size +
            PINT_dist_param_table_alloc_inc;

        buf = malloc(new_table_size * sizeof(PINT_dist_param_offset));
        if (0 != buf)
        {
            if(PINT_dist_param_table_size)
            {
                memcpy(buf, PINT_dist_param_table,
                       PINT_dist_param_table_size * sizeof(PINT_dist_param_offset));
            }
            memset(buf + PINT_dist_param_table_size, 0,
                   PINT_dist_param_table_alloc_inc * sizeof(PINT_dist_param_offset));

            if(PINT_dist_param_table_size)
            {
                free(PINT_dist_param_table);
            }
            PINT_dist_param_table_size = new_table_size;
            PINT_dist_param_table = buf;
        }
        else
        {
            return -1;
        }
    }

    /* Allocate memory for the dist and param name */
    dist_len = strlen(dist_name) + 1;
    param_len = strlen(param_name) + 1;
    PINT_dist_param_table[PINT_dist_param_table_entries].dist_name =
        malloc(dist_len);
    if (0 == PINT_dist_param_table[PINT_dist_param_table_entries].dist_name)
    {
        return -1;
    }
    
    PINT_dist_param_table[PINT_dist_param_table_entries].param_name =
        malloc(param_len);
    if (0 == PINT_dist_param_table[PINT_dist_param_table_entries].param_name)
    {
        return -1;
    }    
    
    /* Register the parameter information for later lookup */
    strncpy(PINT_dist_param_table[PINT_dist_param_table_entries].dist_name,
           dist_name, dist_len);
    strncpy(PINT_dist_param_table[PINT_dist_param_table_entries].param_name,
           param_name, param_len);
    PINT_dist_param_table[PINT_dist_param_table_entries].offset = offset;
    PINT_dist_param_table[PINT_dist_param_table_entries].size = field_size;
    PINT_dist_param_table_entries += 1;
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
