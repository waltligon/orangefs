/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS_DISTRIBUTION_H
#define __PVFS_DISTRIBUTION_H

#include "pvfs2-types.h"

/* this struct is used to define a distribution to PVFS */

typedef struct PVFS_Dist {
	char *dist_name;
	int name_size;
	int param_size;
	struct PVFS_Dist_params *params;
	struct PVFS_Dist_methods *methods;
} PVFS_Dist;

struct PVFS_Dist *PVFS_Dist_create(char *name);

int PVFS_Dist_free(PVFS_Dist *dist);

struct PVFS_Dist *PVFS_Dist_copy(PVFS_Dist *dist);

int PVFS_Dist_getparams(void *buf, PVFS_Dist *dist);

int PVFS_Dist_setparams(PVFS_Dist *dist, void *buf);

/******** macros for access to dist struct ***********/

#define PINT_DIST_PACK_SIZE(distp)\
	((distp)->name_size + (distp)->param_size + sizeof(struct PVFS_Dist))

#endif /* __PVFS_DISTRIBUTION_H */
