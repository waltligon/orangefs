/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef TROVE_LEDGER_H
#define TROVE_LEDGER_H

#include <sys/types.h>
#include "trove-types.h"

#include "trove-extentlist.h"

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
struct handle_ledger *trove_handle_ledger_init(TROVE_coll_id coll_id, char *admin_name);
void trove_handle_ledger_show(struct handle_ledger *hl);
int trove_handle_ledger_dump(struct handle_ledger *hl);
void trove_handle_ledger_free(struct handle_ledger *hl);

/* handle_get,put - obtain, return a handle from/to a particular handle ledger.
 */
TROVE_handle trove_handle_get(struct handle_ledger *hl, TROVE_handle requested_handle, TROVE_handle bitmask);
int trove_handle_put(struct handle_ledger *hl, TROVE_handle handle);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */

#endif

