
/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_VERSION_BUFFER_H__
#define __DBPF_VERSION_BUFFER_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "pvfs2-types.h"
#include "dbpf.h"

enum dbpf_version_buffer_type
{
    DBPF_VERSION_BUFFER_FD = 1,
    DBPF_VERSION_BUFFER_MEM = 2
};

typedef struct
{
    union
    {
        int fd;
        void * memp;
    } u;

    enum dbpf_version_buffer_type type;
    TROVE_size size;
    uint32_t version;
} dbpf_version_buffer_ref;

int dbpf_version_buffer_create(TROVE_coll_id coll_id,
                               TROVE_handle handle,
                               uint32_t version,
                               char ** mem_regions,
                               TROVE_size * mem_sizes,
                               int mem_count,
                               dbpf_version_buffer_ref * refp);

int dbpf_version_buffer_get(dbpf_version_buffer_ref * ref,
                            char ** mem,
                            TROVE_size * size);

int dbpf_version_buffer_destroy(
        TROVE_coll_id coll_id,
        TROVE_handle handle,
        dbpf_version_buffer_ref * ref);

#if defined(__cplusplus)
}
#endif

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
