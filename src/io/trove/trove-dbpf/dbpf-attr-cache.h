/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_ATTR_CACHE_H
#define __DBPF_ATTR_CACHE_H

#include "dbpf.h"
#include "trove-types.h"

#define DBPF_ATTR_CACHE_INVALID_SIZE -1

/* all methods return 0 on success; -1 on failure unless noted */

/*
  table size is the hash table size;
  cache_max_num_elems bounds the number of elems stored
  in that hash table (or can be...someday)
*/
int dbpf_attr_cache_initialize(int table_size, int cache_max_num_elems);

/* returns the looked up ds_attr on success; NULL on failure */
TROVE_ds_attributes *dbpf_attr_cache_lookup(TROVE_handle key);

int dbpf_attr_cache_insert(TROVE_handle key, TROVE_ds_attributes *attr);
int dbpf_attr_cache_remove(TROVE_handle key);
int dbpf_attr_cache_finalize(void);

#endif /* __DBPF_ATTR_CACHE_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
