/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pvfs2-types.h>
#include <pint-distribution.h>
#include <pvfs-distribution.h>

/* compiled-in distributions */
extern PVFS_Dist default_dist;
extern PVFS_Dist simple_stripe_dist;

/* initial dist table - default dists */
PVFS_Dist *PINT_Dist_table[PINT_DIST_TABLE_SZ] = {
	&default_dist,
	&simple_stripe_dist,
	NULL
};

int PINT_Dist_count = 2; /* global size of dist table */

/*
 * add a dist to dist table
 */
int PINT_register_distribution(struct PVFS_Dist *d_p)
{
	if (PINT_Dist_count < PINT_DIST_TABLE_SZ)
	{
		PINT_Dist_table[PINT_Dist_count++] = d_p;
		return (0);
	}
	return (-1);
}

/*
 * remove a dist from dist table
 */
int PINT_unregister_distribution(char *dist_name)
{
	int d;
   if (!dist_name)
	      return -1;
   for (d = 0; d < PINT_Dist_count && PINT_Dist_table[d]; d++)
   {
		if (!strncmp(dist_name, PINT_Dist_table[d]->dist_name,
					PINT_DIST_NAME_SZ))
		{
			PINT_Dist_table[d]->dist_name[0] = 0; /* bad cheese here-WBL */
			return (0);
		}
	}
	return (-1);
}

/*
 * pass in a dist with a valid name, looks up in dist table
 * if found, fills in the missing parts of the structure
 */
int PINT_Dist_lookup(PVFS_Dist *dist)
{
	int d;
	if (!dist || !dist->dist_name)
		return -1;
	for (d = 0; d < PINT_Dist_count && PINT_Dist_table[d]; d++)
	{
		if (!strncmp(dist->dist_name, PINT_Dist_table[d]->dist_name,
					PINT_DIST_NAME_SZ))
		{
			dist->name_size = PINT_Dist_table[d]->name_size;
			dist->param_size = PINT_Dist_table[d]->param_size;
			if (!dist->params)
				dist->params = PINT_Dist_table[d]->params;
			if (!dist->methods)
				dist->methods = PINT_Dist_table[d]->methods;
			return 0; /* success */
		}
	}
	return -1;
}

/*
 * If buffer is not null, copy dist into buffer, then convert
 * pointers to integers in buffer, leave dist alone
 * if buffer is null, convert pointers to integers in dist
 * with no copy - dist is modified
 */
void PINT_Dist_encode(void *buffer, PVFS_Dist *dist)
{
	PVFS_Dist* old_dist = dist;
	
	if (!dist)
		return;
	if (buffer)
	{
		memcpy(buffer, dist,
				sizeof(struct PVFS_Dist) + dist->name_size + dist->param_size);
		dist = buffer;
		/* adjust pointers in new buffer */
		dist->dist_name = ((char*)dist + ((char*)old_dist->dist_name -
			(char*)old_dist));
		dist->params = ((char*)dist + ((char*)old_dist->params - 
			(char*)old_dist));
	}
	/* convert pointers in dist to ints */
	(int)(dist->dist_name) = (char *)(dist->dist_name) - (char *)(dist);
	(int)(dist->params) = (char *)(dist->params) - (char *)(dist);
	dist->methods = NULL;
}

/*
 * If buffer is not null, copy buffer into dist, then convert
 * integers to pointers in dist, leave buffer alone
 * if buffer is null, convert integers to pointers in dist
 * with no copy - dist is modified
 */
void PINT_Dist_decode(PVFS_Dist *dist, void *buffer)
{
	if (!dist)
		return;
	if (buffer)
	{
		PVFS_Dist *d2 = (PVFS_Dist *)buffer;
		memcpy(dist, buffer, 
				sizeof(struct PVFS_Dist) + d2->name_size + d2->param_size);
	}
	/* convert pointers in dist to ints */
	dist->dist_name = (char *)(dist) + (int)(dist->dist_name);
	dist->params = (struct PVFS_Dist_params *)
		((char *)(dist) + (int)(dist->params));
	dist->methods = NULL;
}
