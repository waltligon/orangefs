/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_KEYVAL_H__
#define __DBPF_KEYVAL_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <db.h>

#include "trove.h"
#include "dbpf.h"

void dbpf_keyval_dbcache_initialize(void);

void dbpf_keyval_dbcache_finalize(void);

int dbpf_keyval_dbcache_try_remove(TROVE_coll_id coll_id,
				   TROVE_handle handle);

int dbpf_keyval_dbcache_try_get(TROVE_coll_id coll_id,
				TROVE_handle handle,
				int create_flag,
				DB **db_pp);

void dbpf_keyval_dbcache_put(TROVE_coll_id coll_id,
			     TROVE_handle handle);				

#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */

#endif
