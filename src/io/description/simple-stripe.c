/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pvfs2-types.h>

#include <simple-stripe.h>

static PVFS_offset logical_to_physical_offset (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	return (((logical_offset / dparam->strip_size) / iod_count)
			* dparam->strip_size) + (logical_offset % dparam->strip_size);
}

static PVFS_offset physical_to_logical_offset (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset physical_offset)
{
	return ((((physical_offset / dparam->strip_size) * iod_count) + iod_num)
			* dparam->strip_size) + (physical_offset % dparam->strip_size);
}

static PVFS_offset next_mapped_offset (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	PVFS_offset diff; /* distance of loff from beginning of strip in this stripe */
	PVFS_size   stripe_size; /* not to be confused with strip size */
	PVFS_offset iod_starting_offset; /* offset of strip from start of stripe */

	iod_starting_offset = iod_num * dparam->strip_size;
	stripe_size = iod_count * dparam->strip_size;
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
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset physical_offset)
{
	return dparam->strip_size - (physical_offset % dparam->strip_size);
}

#if 0
/* this is old stuff that will probably be removed shortly - WBL */
static PVFS_size server_number (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	return (logical_offset / dparam->strip_size) % iod_count;
}

static PVFS_size contiguous_size (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	return dparam->strip_size - (logical_offset % dparam->strip_size);
}
#endif

static PVFS_size logical_file_size (PVFS_Dist_params *dparam,
		PVFS_count32 iod_count, PVFS_size *psizes)
{
	/* take the max of the max offset on each server */
	PVFS_size max;
	int s;
	if (!psizes)
		return -1;
	max = 0;
	for (s = 0; s < iod_count; s++)
	{
		PVFS_size smax;
		PVFS_size disp;
		PVFS_size stripes;
		stripes = psizes[s] / dparam->strip_size;
		disp = psizes[s] % dparam->strip_size;
		smax = disp + ((((stripes - 1) * iod_count) + s) * dparam->strip_size);
		if (smax > max)
			max = smax;
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
	decode
};

PVFS_Dist simple_stripe_dist = {
	"simple_stripe",
	14, /* name size */
	sizeof(struct PVFS_Dist_params), /* param size */
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
