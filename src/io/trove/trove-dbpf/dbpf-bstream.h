/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_BSTREAM_H__
#define __DBPF_BSTREAM_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <aio.h>

#include <trove.h>
#include <dbpf.h>


/* bstream-aio functions */

int dbpf_bstream_listio_convert(
				int fd,
				int op_type,
				char **mem_offset_array,
				TROVE_size *mem_size_array,
				int mem_count,
				TROVE_offset *stream_offset_array,
				TROVE_size *stream_size_array,
				int stream_count,
				struct aiocb *aiocb_array,
				int *aiocb_count,
				struct bstream_listio_state *lio_state
				);

/* bstream-fd-cache functions */
void dbpf_bstream_fdcache_initialize(void);

void dbpf_bstream_fdcache_finalize(void);

int dbpf_bstream_fdcache_get(TROVE_coll_id coll_id,
			     TROVE_handle handle,
			     int create_flag);

void dbpf_bstream_fdcache_put(TROVE_coll_id coll_id,
			      TROVE_handle handle);

#if defined(__cplusplus)
}
#endif

#endif




