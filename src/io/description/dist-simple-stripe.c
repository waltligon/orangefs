/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs-distribution.h"
#include "pvfs2-types.h"
#include "pvfs2-dist-simple-stripe.h"

static PVFS_offset logical_to_physical_offset (void* params,
                                               uint32_t server_nr,
                                               uint32_t server_ct,
                                               PVFS_offset logical_offset)
{
    PVFS_offset ret_offset = 0;
    int full_stripes = 0;
    PVFS_size leftover = 0;
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    
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

static PVFS_offset physical_to_logical_offset (void* params,
                                               uint32_t server_nr,
                                               uint32_t server_ct,
                                               PVFS_offset physical_offset)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    PVFS_size strips_div = physical_offset / dparam->strip_size;
    PVFS_size strips_mod = physical_offset % dparam->strip_size;
    PVFS_offset acc = 0;

    acc = (strips_div - 1) * dparam->strip_size * server_ct;
    if(strips_mod)
    {
        acc += dparam->strip_size * server_ct;
        acc += dparam->strip_size * server_nr;
        acc += strips_mod;
    }
    else
    {
        acc += dparam->strip_size * (server_nr+1);
    }
    return(acc);
}

static PVFS_offset next_mapped_offset (void* params,
                                       uint32_t server_nr,
                                       uint32_t server_ct,
                                       PVFS_offset logical_offset)
{
    PVFS_offset diff; /* distance of loff from beginning of strip in this stripe */
    PVFS_size   stripe_size; /* not to be confused with strip size */
    PVFS_offset server_starting_offset; /* offset of strip from start of stripe */
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;

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
                                   uint32_t server_nr,
                                   uint32_t server_ct,
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

    if (!psizes)
        return -1;
    for (s = 0; s < server_ct; s++)
    {
        tmp_max = physical_to_logical_offset(dparam,
                                             s, server_ct, psizes[s]);
        if(tmp_max > max)
            max = tmp_max;
    }
    return max;
}  

static void encode(void* params, void *buffer)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    memcpy(buffer, dparam, sizeof(PVFS_simple_stripe_params));
}

static void decode (void* params, void *buffer)
{
    PVFS_simple_stripe_params* dparam = (PVFS_simple_stripe_params*)params;
    memcpy(dparam, buffer, sizeof(PVFS_simple_stripe_params));
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

static PVFS_simple_stripe_params simple_stripe_params = {
	65536 /* stripe size */
};

static PVFS_Dist_methods simple_stripe_methods = {
    logical_to_physical_offset,
    physical_to_logical_offset,
    next_mapped_offset,
    contiguous_length,
    logical_file_size,
    encode,
    decode,
    encode_lebf,
    decode_lebf,
};

PVFS_Dist simple_stripe_dist = {
    PVFS_DIST_SIMPLE_STRIPE_NAME,
    roundup8(PVFS_DIST_SIMPLE_STRIPE_NAME_SIZE),   /* name size */
    roundup8(sizeof(PVFS_simple_stripe_params)), /* param size */
    &simple_stripe_params,
    &simple_stripe_methods
};

#ifdef MODULE

void init_module()
{
	 PVFS_register_distribution(&simple_stripe_dist);
}

void cleanup_module()
{
	 PVFS_unregister_distribution(PVFS_DIST_SIMPLE_STRIPE_NAME);
}

#endif
