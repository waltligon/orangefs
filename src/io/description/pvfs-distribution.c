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
 * Looks up a distribution and copies it into a contiguous memory region
 */
PVFS_Dist *PVFS_Dist_create(char *name)
{
	//int name_size;
	char *newname;
	PVFS_Dist old_dist;
	PVFS_Dist *new_dist;
	if (!name)
		return NULL;
	//name_size = strnlen(name, PINT_DIST_NAME_SZ);
	//name[name_size] = 0;
	old_dist.dist_name = name;
	old_dist.params = NULL;
	old_dist.methods = NULL;
	if (PINT_Dist_lookup(&old_dist) == 0)
	{
		/* distribution was found */
		new_dist = (PVFS_Dist *)malloc(PINT_DIST_PACK_SIZE(&old_dist));
		if (new_dist)
		{
			*new_dist = old_dist;
			newname = (char *)(new_dist + 1);
			new_dist->dist_name = strncpy(newname, name, old_dist.name_size);
			newname[old_dist.name_size-1] = 0; /* force a terminating char */
			new_dist->params =
				(PVFS_Dist_params *)(((char *)(new_dist + 1)) + old_dist.name_size);
			memcpy(new_dist->params, old_dist.params, old_dist.param_size);
			new_dist->methods = NULL; /* can look 'em up later */
			return (new_dist);
		}
		/* memory allocation failed */
		return NULL;
	}
	else
	{
		/* distribution was not found */
		return NULL;
	}
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

PVFS_Dist *PVFS_Dist_copy(PVFS_Dist *dist)
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
		return (new_dist);
	}
	/* memory allocation failed */
	return NULL;
}

int PVFS_Dist_getparams(void *buf, PVFS_Dist *dist)
{
	if (!dist || !buf)
	{
		return -1;
	}
	memcpy (buf, dist->params, dist->param_size);
	return 0;
}

int PVFS_Dist_setparams(PVFS_Dist *dist, void *buf)
{
	if (!dist || !buf)
	{
		return -1;
	}
	memcpy (dist->params, buf, dist->param_size);
	return 0;
}
