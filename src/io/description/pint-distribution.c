/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pvfs2-types.h"
#include "pint-distribution.h"
#include "pvfs-distribution.h"
#include "pvfs2-debug.h"
#include "gossip.h"

/* compiled-in distributions */
/* extern struct PVFS_Dist default_dist; */
extern struct PVFS_Dist simple_stripe_dist;

/* initial dist table - default dists */
struct PVFS_Dist *PINT_Dist_table[PINT_DIST_TABLE_SZ] = {
    /*&default_dist,*/
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
int PINT_Dist_lookup(struct PVFS_Dist *dist)
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
void PINT_Dist_encode(void *buffer, struct PVFS_Dist *dist)
{
	PVFS_Dist* old_dist = dist;
	
	if (!dist)
		return;
	if (buffer)
	{
		memcpy(buffer, dist, PINT_DIST_PACK_SIZE(dist));
		dist = buffer;
		/* adjust pointers in new buffer */
		dist->dist_name = ((char *)dist + ((char *)old_dist->dist_name -
			(char *)old_dist));
		dist->params = (struct PVFS_Dist_params *)
			((char *)dist + ((char *)old_dist->params - (char *)old_dist));
	}
	/* convert pointers in dist to ints */
	dist->dist_name = (void *) (dist->dist_name - (char *) dist);
	dist->params = (void *) ((char *) dist->params - (char *) dist);
	dist->methods = NULL;
}

/*
 * If buffer is not null, copy buffer into dist, then convert
 * integers to pointers in dist, leave buffer alone
 * if buffer is null, convert integers to pointers in dist
 * with no copy - dist is modified
 */
void PINT_Dist_decode(struct PVFS_Dist *dist, void *buffer)
{
	if (!dist)
		return;
	if (buffer)
	{
		PVFS_Dist *d2 = (PVFS_Dist *)buffer;
		memcpy(dist, buffer, PINT_DIST_PACK_SIZE(d2));
	}
	/* convert ints in dist to pointers */
	dist->dist_name = (char *) dist + (unsigned long) dist->dist_name;
	dist->params = (void *) ((char *) dist + (unsigned long) dist->params);
	/* set methods */
	dist->methods = NULL;
	if (PINT_Dist_lookup(dist)) {
	    gossip_err("%s: lookup dist failed\n", __func__);
	    exit(1);
	}
}

void PINT_Dist_dump(PVFS_Dist *dist)
{
	gossip_debug(GOSSIP_DIST_DEBUG,"******************************\n");
	gossip_debug(GOSSIP_DIST_DEBUG,"address\t\t%p\n", dist);
	gossip_debug(GOSSIP_DIST_DEBUG,"dist_name\t%s\n", dist->dist_name);
	gossip_debug(GOSSIP_DIST_DEBUG,"name_size\t%d\n", dist->name_size);
	gossip_debug(GOSSIP_DIST_DEBUG,"param_size\t%d\n", dist->param_size);
	gossip_debug(GOSSIP_DIST_DEBUG,"params\t\t%p\n", dist->params);
	gossip_debug(GOSSIP_DIST_DEBUG,"methods\t\t%p\n", dist->methods);
	gossip_debug(GOSSIP_DIST_DEBUG,"******************************\n");
}
