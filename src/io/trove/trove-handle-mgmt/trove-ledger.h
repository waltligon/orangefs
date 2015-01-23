/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef TROVE_LEDGER_H
#define TROVE_LEDGER_H

#include "pvfs2-internal.h"

#include <sys/types.h>
#include "trove-types.h"

#include "trove-extentlist.h"

/* struct handle_ledger
 *
 * Structure used internally for maintaining state.
 */
struct handle_ledger {
    struct TROVE_handle_extentlist free_list;
    struct TROVE_handle_extentlist recently_freed_list;
    struct TROVE_handle_extentlist overflow_list;
    char *store_name;
    FILE *backing_store;
    TROVE_handle free_list_handle;
    TROVE_handle recently_freed_list_handle;
    TROVE_handle overflow_list_handle;
    uint64_t cutoff;    /* when to start trying to reuse handles */
};

enum {
	FREE_EXTENTLIST_HANDLE = 1,
	RECENTLY_FREED_EXTENTLIST_HANDLE,
	OVERFLOW_EXTENTLIST_HANDLE
};

/* handle_ledger_xxx - functions for initializing, storing to disk, and
 * freeing handle ledgers (lists of free handles).
 *
 * An opaque reference to the handle ledger is returned, as we could have
 * more than one of these in use at any time.
 */
struct handle_ledger *trove_handle_ledger_init(
    TROVE_coll_id coll_id,
    char *admin_name);
void trove_handle_ledger_show(
    struct handle_ledger *hl);
int trove_handle_ledger_dump(
    struct handle_ledger *hl);
void trove_handle_ledger_free(
    struct handle_ledger *hl);

/* trove_handle_ledger_addextent:  add a new legal extent from which the ledger
 *   can dole out handles.
 *
 *      hl      struct handle_ledger to which we add stuff
 *      extent  the new legal extent
 *
 * return:
 *    0 if ok
 *    nonzero if not
 */
static inline int trove_handle_ledger_addextent(struct handle_ledger *hl,
        TROVE_extent * extent)
{
   return extentlist_addextent(&(hl->free_list),
           extent->first, extent->last);
}

/* trove_handle_remove:
 *      take a specific handle out of the valid handle space
 *
 * returns
 *  0 if ok
 *  nonzero  on error
 */

static inline int trove_handle_remove(struct handle_ledger *hl,
    TROVE_handle handle)
{
    return extentlist_handle_remove(&(hl->free_list), handle);
}

/*
  handle_get,put - obtain, return a handle from/to a particular handle
  ledger.
*/
TROVE_handle trove_ledger_handle_alloc(
    struct handle_ledger *hl);
TROVE_handle trove_ledger_handle_alloc_from_range(
    struct handle_ledger *hl,
    TROVE_extent *extent);
int trove_ledger_peek_handles(
    struct handle_ledger *hl,
    TROVE_handle *out_handle_array,
    int max_num_handles,
    int *returned_handle_count);
int trove_ledger_peek_handles_from_extent(
    struct handle_ledger *hl,
    TROVE_extent *extent,
    TROVE_handle *out_handle_array,
    int max_num_handles,
    int *returned_handle_count);
int trove_ledger_handle_free(
    struct handle_ledger *hl,
    TROVE_handle handle);
/* trove_handle_ledger_set_threshold()
 * hl:  handle ledger object we will modify
 * nhandles:  number of total handles in the system. we will make a cutoff
 *                 value based upon this number
 */
static inline void trove_handle_ledger_set_threshold(struct handle_ledger *hl,
    uint64_t nhandles)
{
    hl->cutoff = (nhandles/4)+1;
}
int trove_ledger_set_timeout(
    struct handle_ledger *hl, 
    struct timeval *timeout);
void trove_handle_ledger_get_statistics(
    struct handle_ledger *hl,
    uint64_t *free_count);
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

