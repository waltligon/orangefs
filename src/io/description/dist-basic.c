/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pvfs-distribution.h"
#include "pvfs2-types.h"
#include "pvfs2-dist-basic.h"

#define CONTIGBLOCKSZ 65536

/* in this distribution all data is stored on a single server */

static PVFS_offset logical_to_physical_offset(void* params,
                                              uint32_t server_nr,
                                              uint32_t server_ct,
                                              PVFS_offset logical_offset)
{
    return logical_offset;
}

static PVFS_offset physical_to_logical_offset(void* params,
                                              uint32_t server_nr,
                                              uint32_t server_ct,
                                              PVFS_offset physical_offset)
{
    return physical_offset;
}

static PVFS_offset next_mapped_offset(void* params,
                                      uint32_t server_nr,
                                      uint32_t server_ct,
                                      PVFS_offset logical_offset)
{
    return logical_offset;
}

static PVFS_size contiguous_length(void* params,
                                   uint32_t server_nr,
                                   uint32_t server_ct,
                                   PVFS_offset physical_offset)
{
    return CONTIGBLOCKSZ;
}

static PVFS_size logical_file_size(void* params,
                                   uint32_t server_ct,
                                   PVFS_size *psizes)
{
    if (!psizes)
        return -1;
    return psizes[0];
}

static void encode(void* params, void *buffer)
{
    memcpy(buffer, params, sizeof(PVFS_basic_params));
}

static void decode(void* params, void *buffer)
{
	memcpy(params, buffer, sizeof(PVFS_basic_params));
}

static void encode_lebf(char **pptr, void* params)
{
}

static void decode_lebf(char **pptr, void* params)
{
}

static PVFS_basic_params basic_params;

static PVFS_Dist_methods basic_methods = {
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

PINT_dist basic_dist = {
    PVFS_DIST_BASIC_NAME,
    roundup8(PVFS_DIST_BASIC_NAME_SIZE), /* name size */
    0, /* param size */
    &basic_params,
    &basic_methods
};

#ifdef MODULE

void init_module()
{
    PVFS_register_distribution(&basic_dist);
}

void cleanup_module()
{
    PVFS_unregister_distribution(PVFS_DIST_BASIC_NAME);
}

#endif
