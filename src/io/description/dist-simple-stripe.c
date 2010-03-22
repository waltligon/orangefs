/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pint-distribution.h"
#include "pint-dist-utils.h"
#include "pvfs2-types.h"
#include "pvfs2-dist-simple-stripe.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"

static PVFS_offset logical_to_physical_offset (void* params,
                                               PINT_request_file_data* fd,
                                               PVFS_offset logical_offset)
{
    PVFS_offset ret_offset = 0;
    int full_stripes = 0;
    PVFS_size leftover = 0;
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;
    
    /* how many complete stripes are in there? */
    full_stripes = logical_offset / (dparam->strip_size*server_ct);
    ret_offset += full_stripes * dparam->strip_size;
    
    /* do the leftovers fall within our region? */
    leftover = logical_offset - full_stripes * dparam->strip_size * server_ct;
    if(leftover >= server_nr*dparam->strip_size)
    {
        /* if so, tack that on to the physical offset as well */
        if(leftover < (server_nr + 1) * dparam->strip_size)
            ret_offset += leftover - (server_nr * dparam->strip_size);
        else
            ret_offset += dparam->strip_size;
    }
    return(ret_offset);
}

static PVFS_size physical_to_logical_size(void * params,
                                          PINT_request_file_data * fd,
                                          PVFS_size physical_size)
{
    PVFS_simple_stripe_params * dparam = (PVFS_simple_stripe_params *)params;
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;
    
    /* The algorithm for this one turns out to be kinda tricky.
     * We basically have 3 components that are summed:
     *
     * 1. The size of the complete stripes (a stripe being the strip size times
     *    the number of servers).  In most cases this value is the physical
     *    size / strip size (times the stripe size).  
     *    But when the physical size equals strip size
     *    we need to subtract one (we're still in that stripe).  Also, since
     *    we subtract one, when the physical size is zero, we need to verify
     *    that div is a non-negative value.
     *
     * 2. The size of the strips in servers before the current server.  Simply
     *    the strip size times the server number (starting from zero).
     *
     * 3. The remainder size within the current strip.  In most cases its 
     *    physical size modulo strip size, but again, if the physical
     *    size equals the strip size, we need the strip size.
     */
    PVFS_size physical_size_n1 = (physical_size ? (physical_size - 1) : 0);

    PVFS_size strips_div = physical_size_n1 / dparam->strip_size;
    PVFS_size strips_mod = (physical_size_n1 % dparam->strip_size ) + 1;

    return (strips_div * dparam->strip_size * server_ct) +
           (dparam->strip_size * server_nr) +
           strips_mod;
}

static PVFS_offset physical_to_logical_offset (void* params,
                                               PINT_request_file_data* fd,
                                               PVFS_offset physical_offset)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;
    PVFS_size strips_div = physical_offset / dparam->strip_size;
    PVFS_size strips_mod = physical_offset % dparam->strip_size;

    return (strips_div * dparam->strip_size * server_ct) +
           (dparam->strip_size * server_nr) +
           strips_mod;
}

static PVFS_offset next_mapped_offset (void* params,
                                       PINT_request_file_data* fd,
                                       PVFS_offset logical_offset)
{
    PVFS_offset diff; /* distance of loff from beginning of strip in this stripe */
    PVFS_size   stripe_size; /* not to be confused with strip size */
    PVFS_offset server_starting_offset; /* offset of strip from start of stripe */
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;

    server_starting_offset = server_nr * dparam->strip_size;
    stripe_size = server_ct * dparam->strip_size;
    diff = (logical_offset - server_starting_offset) % stripe_size;
    if (diff < 0 ) 
        /* loff is before this strip - move to server_so */
        return server_starting_offset;
    else 
        if (diff >= dparam->strip_size) 
            /* loff is after this strip - go to next strip */
            return logical_offset + (stripe_size - diff);
        else 
            /* loff is within this strip - just return loff */
            return logical_offset;
}

static PVFS_size contiguous_length(void* params,
                                   PINT_request_file_data* fd,
                                   PVFS_offset physical_offset)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    return dparam->strip_size - (physical_offset % dparam->strip_size);
}

static PVFS_size logical_file_size(void* params,
                                   uint32_t server_ct,
                                   PVFS_size *psizes)
{
    /* take the max of the max offset on each server */
    PVFS_size max = 0;
    PVFS_size tmp_max = 0;
    int s = 0;
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    PINT_request_file_data file_data;

    if (!psizes)
        return -1;

    /* Initialize file data struct */
    memset(&file_data, 0, sizeof(file_data));
    file_data.server_ct = server_ct;
    
    for (s = 0; s < server_ct; s++)
    {
        file_data.server_nr = s;
        if(psizes[s])
        {
            tmp_max = physical_to_logical_size(dparam, &file_data, psizes[s]);
            if(tmp_max > max)
                max = tmp_max;
        }
    }
    return max;
}  

static void encode_lebf(char **pptr, void* params)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    encode_PVFS_size(pptr, &dparam->strip_size);
}

static void decode_lebf(char **pptr, void* params)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    decode_PVFS_size(pptr, &dparam->strip_size);
}

static void registration_init(void* params)
{
    PINT_dist_register_param(PVFS_DIST_SIMPLE_STRIPE_NAME, "strip_size",
                             PVFS_simple_stripe_params, strip_size);

}

static char *params_string(void *params)
{
    char param_string[1024];
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;

    sprintf(param_string, "strip_size:%llu\n", llu(dparam->strip_size));
    return strdup(param_string);
}

static PVFS_simple_stripe_params simple_stripe_params = {
    PVFS_DIST_SIMPLE_STRIPE_DEFAULT_STRIP_SIZE /* strip size */
};

static PVFS_size get_blksize(void* params)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    /* report the strip size as the block size */
    return(dparam->strip_size);
}

static PINT_dist_methods simple_stripe_methods = {
    logical_to_physical_offset,
    physical_to_logical_offset,
    next_mapped_offset,
    contiguous_length,
    logical_file_size,
    PINT_dist_default_get_num_dfiles,
    PINT_dist_default_set_param,
    get_blksize,
    encode_lebf,
    decode_lebf,
    registration_init,
    params_string
};

PINT_dist simple_stripe_dist = {
    .dist_name = PVFS_DIST_SIMPLE_STRIPE_NAME,
    .name_size = roundup8(PVFS_DIST_SIMPLE_STRIPE_NAME_SIZE), /* name size */
    .param_size = roundup8(sizeof(PVFS_simple_stripe_params)), /* param size */
    .params = &simple_stripe_params,
    .methods = &simple_stripe_methods
};


/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
