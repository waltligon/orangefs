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

#include <trove.h>
#include <dbpf.h>

void dbpf_keyval_dbcache_initialize(void);

void dbpf_keyval_dbcache_finalize(void);

DB *dbpf_keyval_dbcache_get(TROVE_coll_id coll_id,
			    TROVE_handle handle,
			    int create_flag);

void dbpf_keyval_dbcache_put(TROVE_coll_id coll_id,
			     TROVE_handle handle);				

#if defined(__cplusplus)
}
#endif

#endif
