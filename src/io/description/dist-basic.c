/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pint-distribution.h"
#include "pint-dist-utils.h"
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

static void encode_lebf(char **pptr, void* params)
{
}

static void decode_lebf(char **pptr, void* params)
{
}

static void registration_init(void* params)
{
}


static PVFS_basic_params basic_params;

static PINT_dist_methods basic_methods = {
    logical_to_physical_offset,
    physical_to_logical_offset,
    next_mapped_offset,
    contiguous_length,
    logical_file_size,
    PINT_dist_default_get_num_dfiles,
    PINT_dist_default_set_param,
    encode_lebf,
    decode_lebf,
    registration_init
};

PINT_dist basic_dist = {
    PVFS_DIST_BASIC_NAME,
    roundup8(PVFS_DIST_BASIC_NAME_SIZE), /* name size */
    0, /* param size */
    &basic_params,
    &basic_methods
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
