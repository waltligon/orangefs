/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pvfs2-types.h>

#include <default-dist.h>

#define CONTIGBLOCKSZ 65536

/* in this distribution all data is stored on a single server */

static PVFS_offset logical_to_physical_offset (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	return logical_offset;
}

static PVFS_offset physical_to_logical_offset (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset physical_offset)
{
	return physical_offset;
}

static PVFS_offset next_mapped_offset (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	return logical_offset;
}

static PVFS_size contiguous_length (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset physical_offset)
{
	return CONTIGBLOCKSZ;
}

#if 0
/* this is old stuff that will probably be removed shortly - WBL */
static PVFS_size server_number (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	return 0;
}

static PVFS_size contiguous_size (PVFS_Dist_params *dparam,
		PVFS_count32 iod_num, PVFS_count32 iod_count, PVFS_offset logical_offset)
{
	return CONTIGBLOCKSZ;
}
#endif

static PVFS_size logical_file_size (PVFS_Dist_params *dparam,
		PVFS_count32 iod_count, PVFS_size *psizes)
{
	if (!psizes)
		return -1;
	return psizes[0];
}

static void encode (PVFS_Dist_params *dparam, void *buffer)
{
	memcpy(buffer, dparam, sizeof(PVFS_Dist_params));
}

static void decode (PVFS_Dist_params *dparam, void *buffer)
{
	memcpy(dparam, buffer, sizeof(PVFS_Dist_params));
}

static PVFS_Dist_params default_params;

static PVFS_Dist_methods default_methods = {
	logical_to_physical_offset,
	physical_to_logical_offset,
	next_mapped_offset,
	contiguous_length,
	logical_file_size,
	encode,
	decode
};

PVFS_Dist default_dist = {
	"default_dist",
	13, /* name size */
	0, /* param size */
	&default_params,
	&default_methods
};

#ifdef MODULE

void init_module()
{
	 PVFS_register_distribution(&default_dist);
}

void cleanup_module()
{
	 PVFS_unregister_distribution("default_dist");
}

#endif
