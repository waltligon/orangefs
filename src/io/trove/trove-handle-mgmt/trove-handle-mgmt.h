/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_HANDLE_MGMT_H
#define __TROVE_HANDLE_MGMT_H

#define MAX_NUM_VERIFY_HANDLE_COUNT 512

/*
  NOTE: all methods return -1 on error;
  0 on success  unless otherwise noted
*/

/* public methods */
int trove_handle_mgmt_initialize(void);

int trove_set_handle_ranges(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id,
    char *handle_range_str);

int trove_set_handle_timeout(TROVE_coll_id coll_id,
			TROVE_context_id context_id,
			struct timeval * timeout);

/* returns a valid TROVE_handle on success; 0 otherwise */
TROVE_handle trove_handle_alloc(TROVE_coll_id coll_id);

/* returns a valid TROVE_handle from the range-set given in the
 * PVFS_handle_extent_array; 0 otherwise */
TROVE_handle trove_handle_alloc_from_range(
    TROVE_coll_id, 
    TROVE_handle_extent_array *extent_array);

int trove_handle_set_used(
    TROVE_coll_id coll_id,
    TROVE_handle handle);

int trove_handle_free(
    TROVE_coll_id coll_id,
    TROVE_handle handle);

int trove_handle_mgmt_finalize(void);

int trove_handle_get_statistics(TROVE_coll_id coll_id, uint64_t* free_count);

#endif /* __TROVE_HANDLE_MGMT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
