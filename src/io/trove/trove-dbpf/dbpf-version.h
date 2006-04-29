
/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_VERSION_H__
#define __DBPF_VERSION_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include "pvfs2-types.h"
#include "dbpf.h"

int dbpf_version_initialize(void);
int dbpf_version_finalize(void);

int dbpf_version_add(TROVE_coll_id coll_id,
                     TROVE_handle handle,
                     TROVE_vtag_s * vtag,
                     char ** mem_buffers,
                     TROVE_size * mem_sizes,
                     int mem_count,
                     TROVE_offset *stream_offsets,
                     TROVE_size *stream_sizes,
                     int stream_count);

int dbpf_version_find_commit(TROVE_coll_id * coll_id,
                             TROVE_handle * handle,
                             TROVE_offset ** stream_offsets,
                             TROVE_size ** stream_sizes,
                             int * stream_count,
                             char ** membuff,
                             TROVE_size * memsize);

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
