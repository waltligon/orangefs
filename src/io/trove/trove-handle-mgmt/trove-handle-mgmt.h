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
    char *handle_range_str);

/* returns a valid TROVE_handle on success; 0 otherwise */
TROVE_handle trove_handle_alloc(TROVE_coll_id coll_id);

int trove_handle_free(TROVE_coll_id coll_id, TROVE_handle handle);

int trove_handle_mgmt_finalize(void);

#endif /* __TROVE_HANDLE_MGMT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
