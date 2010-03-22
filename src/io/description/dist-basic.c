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
                                              PINT_request_file_data* fd,
                                              PVFS_offset logical_offset)
{
    return logical_offset;
}

static PVFS_offset physical_to_logical_offset(void* params,
                                              PINT_request_file_data* fd,
                                              PVFS_offset physical_offset)
{
    return physical_offset;
}

static PVFS_offset next_mapped_offset(void* params,
                                      PINT_request_file_data* fd,
                                      PVFS_offset logical_offset)
{
    return logical_offset;
}

static PVFS_size contiguous_length(void* params,
                                   PINT_request_file_data* fd,
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

static int get_num_dfiles(void* params,
                          uint32_t num_servers_requested,
                          uint32_t num_dfiles_requested)
{
    return 1;
}

static PVFS_size get_blksize(void* params)
{
    /* this is arbitrary; all data is on one server */
    return CONTIGBLOCKSZ;
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

static char *params_string(void *params)
{
    return strdup("none");
}


static PVFS_basic_params basic_params;

static PINT_dist_methods basic_methods = {
    logical_to_physical_offset,
    physical_to_logical_offset,
    next_mapped_offset,
    contiguous_length,
    logical_file_size,
    get_num_dfiles,
    PINT_dist_default_set_param,
    get_blksize,
    encode_lebf,
    decode_lebf,
    registration_init,
    params_string
};

PINT_dist basic_dist = {
    .dist_name = PVFS_DIST_BASIC_NAME,
    .name_size = roundup8(PVFS_DIST_BASIC_NAME_SIZE), /* name size */
    .param_size = 0, /* param size */
    .params = &basic_params,
    .methods = &basic_methods
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
