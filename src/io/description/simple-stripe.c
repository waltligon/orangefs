/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include <pvfs2-types.h>

#include <simple-stripe.h>

static PVFS_offset logical_to_physical_offset (PVFS_Dist_params *dparam,
		uint32_t server_nr, uint32_t server_ct, PVFS_offset logical_offset)
{
    PVFS_offset ret_offset = 0;
    int full_stripes = 0;
    PVFS_size leftover = 0;

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

#if 0
    return (((logical_offset / dparam->strip_size) / server_ct)
	* dparam->strip_size) + (logical_offset % dparam->strip_size);
#endif
}

static PVFS_offset physical_to_logical_offset (PVFS_Dist_params *dparam,
		uint32_t server_nr, uint32_t server_ct, PVFS_offset physical_offset)
{
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
#if 0
	return ((((physical_offset / dparam->strip_size) * server_ct) + server_nr)
			* dparam->strip_size) + (physical_offset % dparam->strip_size);
#endif
}

static PVFS_offset next_mapped_offset (PVFS_Dist_params *dparam,
		uint32_t server_nr, uint32_t server_ct, PVFS_offset logical_offset)
{
	PVFS_offset diff; /* distance of loff from beginning of strip in this stripe */
	PVFS_size   stripe_size; /* not to be confused with strip size */
	PVFS_offset iod_starting_offset; /* offset of strip from start of stripe */

	iod_starting_offset = server_nr * dparam->strip_size;
	stripe_size = server_ct * dparam->strip_size;
	diff = (logical_offset - iod_starting_offset) % stripe_size;
	if (diff < 0 ) 
		/* loff is before this strip - move to iod_so */
		return iod_starting_offset;
	else 
		if (diff >= dparam->strip_size) 
			/* loff is after this strip - go to next strip */
			return logical_offset + (stripe_size - diff);
		else 
			/* loff is within this strip - just return loff */
			return logical_offset;
}

static PVFS_size contiguous_length (PVFS_Dist_params *dparam,
		uint32_t server_nr, uint32_t server_ct, PVFS_offset physical_offset)
{
	return dparam->strip_size - (physical_offset % dparam->strip_size);
}

#if 0
/* this is old stuff that will probably be removed shortly - WBL */
static PVFS_size server_number (PVFS_Dist_params *dparam,
		uint32_t server_nr, uint32_t server_ct, PVFS_offset logical_offset)
{
	return (logical_offset / dparam->strip_size) % server_ct;
}

static PVFS_size contiguous_size (PVFS_Dist_params *dparam,
		uint32_t server_nr, uint32_t server_ct, PVFS_offset logical_offset)
{
	return dparam->strip_size - (logical_offset % dparam->strip_size);
}
#endif

static PVFS_size logical_file_size (PVFS_Dist_params *dparam,
		uint32_t server_ct, PVFS_size *psizes)
{
	/* take the max of the max offset on each server */
	PVFS_size max = 0;
	PVFS_size tmp_max = 0;
	int s = 0;
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

static void encode (PVFS_Dist_params *dparam, void *buffer)
{
	memcpy(buffer, dparam, sizeof(PVFS_Dist_params));
}

static void decode (PVFS_Dist_params *dparam, void *buffer)
{
	memcpy(dparam, buffer, sizeof(PVFS_Dist_params));
}

static void encode_lebf(char **pptr, PVFS_Dist_params *dparam)
{
    encode_PVFS_size(pptr, &dparam->strip_size);
}

static void decode_lebf(char **pptr, PVFS_Dist_params *dparam)
{
    decode_PVFS_size(pptr, &dparam->strip_size);
}

static PVFS_Dist_params simple_stripe_params = {
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
	"simple_stripe",
	roundup8(14), /* name size */
	roundup8(sizeof(struct PVFS_Dist_params)), /* param size */
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
	 PVFS_unregister_distribution("simple_stripe");
}

#endif
