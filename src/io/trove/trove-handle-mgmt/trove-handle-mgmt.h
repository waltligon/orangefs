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
int trove_handle_mgmt_initialize();

int trove_set_handle_ranges(
    TROVE_coll_id coll_id,
    char *handle_range_str);

int trove_handle_mgmt_finalize();

#endif /* __TROVE_HANDLE_MGMT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
