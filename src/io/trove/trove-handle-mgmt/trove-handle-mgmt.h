/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __TROVE_HANDLE_MGMT_H
#define __TROVE_HANDLE_MGMT_H

#define MAX_NUM_VERIFY_HANDLE_COUNT        4096

#define TROVE_DEFAULT_HANDLE_PURGATORY_SEC 360

/*
  public methods.  all methods return -1 on error; 0 on success unless
  otherwise noted
*/
int trove_handle_mgmt_initialize(void);

int trove_set_handle_ranges(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id,
    char *handle_range_str);

int trove_set_handle_timeout(
    TROVE_coll_id coll_id,
    TROVE_context_id context_id,
    struct timeval * timeout);

/*
  returns a valid TROVE_handle on success and TROVE_HANDLE_NULL
  otherwise (e.g.. no more free handles)
*/
TROVE_handle trove_handle_alloc(TROVE_coll_id coll_id);

/*
  returns a valid TROVE_handle from the range-set given in the
  PVFS_handle_extent_array; TROVE_HANDLE_NULL otherwise
*/
TROVE_handle trove_handle_alloc_from_range(
    TROVE_coll_id, 
    TROVE_handle_extent_array *extent_array);

/*
  fills in some number of handles that are not removed from the handle
  allocator's free list and guarantees that subsequent calls to
  trove_handle_alloc will return these handles in that order.  NOTE:
  this list must NOT be stored or relied on because it could change as
  calls to trove_handle_alloc and trove_handle_alloc_from_range calls
  are made.  return value is 0 on success, non-zero on failure
*/
int trove_handle_peek(
    TROVE_coll_id coll_id,
    TROVE_handle *out_handle_array,
    int max_num_handles,
    int *returned_handle_count);

/*
  same as trove_handle_peek above except that the returned handles are
  limited to the specified ranges.  return value is 0 on success,
  non-zero on failure
*/
int trove_handle_peek_from_range(
    TROVE_coll_id coll_id,
    TROVE_handle_extent_array *extent_array,
    TROVE_handle *out_handle_array,
    int max_num_handles,
    int *returned_handle_count);

int trove_handle_set_used(
    TROVE_coll_id coll_id,
    TROVE_handle handle);

int trove_handle_free(
    TROVE_coll_id coll_id,
    TROVE_handle handle);

int trove_handle_mgmt_finalize(void);

int trove_handle_get_statistics(
    TROVE_coll_id coll_id,
    uint64_t *free_count);

#endif /* __TROVE_HANDLE_MGMT_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
