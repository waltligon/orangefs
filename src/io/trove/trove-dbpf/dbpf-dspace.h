/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_DSPACE_H__
#define __DBPF_DSPACE_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <db.h>

#include <trove.h>
#include <dbpf.h>

void dbpf_dspace_dbcache_initialize(void);

void dbpf_dspace_dbcache_finalize(void);

DB *dbpf_dspace_dbcache_get(TROVE_coll_id coll_id,
			    int create_flag);

void dbpf_dspace_dbcache_put(TROVE_coll_id coll_id);

#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */

#endif
