/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pvfs-distribution.h>
#include <pint-distribution.h>

/*
 * Looks up a distribution and copies it into a contiguous memory region.
 * This is similar to PVFS_Dist_copy, but it copies from the static table,
 * not from another contiguous region.
 */
PVFS_Dist *PVFS_Dist_create(char *name)
{
    PVFS_Dist old_dist;
    PVFS_Dist *new_dist = 0;

    if (!name)
	return 0;
    old_dist.dist_name = name;
    old_dist.params = 0;
    old_dist.methods = 0;
    if (PINT_Dist_lookup(&old_dist) == 0)
    {
	/* distribution was found */
	new_dist = malloc(PINT_DIST_PACK_SIZE(&old_dist));
	if (new_dist)
	{
	    *new_dist = old_dist;
	    new_dist->dist_name
	      = (char *) new_dist + roundup8(sizeof(*new_dist));
	    new_dist->params
	      = (void *)(new_dist->dist_name + roundup8(new_dist->name_size));
	    memcpy(new_dist->dist_name, old_dist.dist_name,
	      old_dist.name_size);
	    memcpy(new_dist->params, old_dist.params, old_dist.param_size);
	    /* leave methods pointing to same static functions */
	}
    }
    return new_dist;
}

int PVFS_Dist_free(PVFS_Dist *dist)
{
	if (dist)
	{
		/* assumes this is a dist created from above */
		free(dist);
		return 0;
	}
	return -1;
}

PVFS_Dist *PVFS_Dist_copy(const PVFS_Dist *dist)
{
	int dist_size;
	PVFS_Dist *new_dist;

	if (!dist)
	{
		return NULL;
	}
	dist_size = PINT_DIST_PACK_SIZE(dist);
	new_dist = (PVFS_Dist *)malloc(dist_size);
	if (new_dist)
	{
		memcpy(new_dist, dist, dist_size);
		/* fixup pointers to new space */
		new_dist->dist_name
		  = (char *) new_dist + roundup8(sizeof(*new_dist));
		new_dist->params
		  = (void *)(new_dist->dist_name + roundup8(new_dist->name_size));
	}
	return (new_dist);
}

int PVFS_Dist_getparams(void *buf, const PVFS_Dist *dist)
{
	if (!dist || !buf)
	{
		return -1;
	}
	memcpy (buf, dist->params, dist->param_size);
	return 0;
}

int PVFS_Dist_setparams(PVFS_Dist *dist, const void *buf)
{
	if (!dist || !buf)
	{
		return -1;
	}
	memcpy (dist->params, buf, dist->param_size);
	return 0;
}
