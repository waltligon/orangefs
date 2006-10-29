#ifndef TASHANDLEMANAGER_H_
#define TASHANDLEMANAGER_H_

#include "red-black-tree.h"
#include "trove.h"

#include "gen-locks.h" 

typedef PVFS_handle_extent tas_handleRange;

void initialiseHandleManager(void);
/*
 * If handle manager should maintain only one handle range for a collection set
 * lowerBound2 = 0
 */
void initialiseCollection(TROVE_coll_id coll_id,TROVE_handle lowerBound, 
	TROVE_handle upperBound,TROVE_handle lowerBound2, TROVE_handle upperBound2);

TROVE_handle getHandle(TROVE_coll_id coll_id,TROVE_handle_extent_array * extends);
TROVE_handle getfixedHandle(TROVE_coll_id coll_id,TROVE_handle handle);
TROVE_handle getArbitraryHandle(TROVE_coll_id coll_id,TROVE_handle handleSep);

int lookupHandle(TROVE_coll_id coll_id,TROVE_handle handle);

int freeHandle(TROVE_coll_id coll_id,TROVE_handle handle);
int freeHandleRange(TROVE_coll_id coll_id,TROVE_handle lowerhandle,TROVE_handle upperhandle);

#endif /*TASHANDLEMANAGER_H_*/
