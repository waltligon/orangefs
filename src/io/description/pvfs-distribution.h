/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS_DISTRIBUTION_H
#define __PVFS_DISTRIBUTION_H

#include <pvfs2-types.h>

/* this struct is used to define a distribution to PVFS */

typedef struct PVFS_Dist {
	char *dist_name;
	int name_size;
	int param_size;
	struct PVFS_Dist_params *params;
	struct PVFS_Dist_methods *methods;
} PVFS_Dist;

struct PVFS_Dist *PVFS_Dist_create(char *name);

#endif /* __PVFS_DISTRIBUTION_H */
