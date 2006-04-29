#ifndef _SYNCH_CACHE_H
#define _SYNCH_CACHE_H

#include <stdio.h>
#include "pvfs2.h"
#include "pvfs2-types.h"

int PINT_synch_cache_init(void);
void PINT_synch_cache_finalize(void);
int PINT_synch_cache_insert(enum PVFS_synch_method method, 
        PVFS_object_ref *ref, void *item);
void* PINT_synch_cache_get(enum PVFS_synch_method method,
        PVFS_object_ref *ref);
int PINT_synch_cache_invalidate(enum PVFS_synch_method method,
        PVFS_object_ref *ref);

#endif
