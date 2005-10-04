/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pvfs2-types.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "pint-distribution.h"


/* global size of dist table */
#define PINT_DIST_TABLE_SZ 8
static int PINT_Dist_count = 0; 

/* compiled-in distributions */
extern PINT_dist basic_dist;
extern PINT_dist simple_stripe_dist;

/* initial dist table - default dists */
PINT_dist* PINT_Dist_table[PINT_DIST_TABLE_SZ] = {0};

/*
 * add a dist to dist table
 */
int PINT_register_distribution(PINT_dist *d_p)
{
    if (0 != d_p &&
        PINT_Dist_count < PINT_DIST_TABLE_SZ)
    {
        /* Register dist */
        PINT_Dist_table[PINT_Dist_count++] = d_p;

        /* Perform dist specific registration code */
        d_p->methods->registration_init(d_p->params);
        
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
 * Looks up a distribution and copies it into a contiguous memory region.
 * This is similar to PVFS_Dist_copy, but it copies from the static table,
 * not from another contiguous region.
 */
PINT_dist *PINT_dist_create(const char *name)
{
    PINT_dist old_dist;
    PINT_dist *new_dist = 0;

    if (!name)
	return 0;
    old_dist.dist_name = (char*)name;
    old_dist.params = 0;
    old_dist.methods = 0;
    if (PINT_dist_lookup(&old_dist) == 0)
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

int PINT_dist_free(PINT_dist *dist)
{
	if (dist)
	{
		/* assumes this is a dist created from above */
		free(dist);
		return 0;
	}
	return -1;
}

PINT_dist* PINT_dist_copy(const PINT_dist *dist)
{
	int dist_size;
	PINT_dist *new_dist;

	if (!dist)
	{
		return NULL;
	}
	dist_size = PINT_DIST_PACK_SIZE(dist);
	new_dist = (PINT_dist *)malloc(dist_size);
	if (new_dist)
	{
		memcpy(new_dist, dist, dist_size);
		/* fixup pointers to new space */
		new_dist->dist_name
		  = (char *) new_dist + roundup8(sizeof(*new_dist));
		new_dist->params
		  = (void *)(new_dist->dist_name + roundup8(new_dist->name_size));
                memcpy(new_dist->dist_name, dist->dist_name,
                       dist->name_size);
                memcpy(new_dist->params, dist->params, dist->param_size);
	}
	return (new_dist);
}

int PINT_dist_getparams(void *buf, const PINT_dist *dist)
{
	if (!dist || !buf)
	{
		return -1;
	}
	memcpy (buf, dist->params, dist->param_size);
	return 0;
}

int PINT_dist_setparams(PINT_dist *dist, const void *buf)
{
	if (!dist || !buf)
	{
		return -1;
	}
	memcpy (dist->params, buf, dist->param_size);
	return 0;
}

/*
 * pass in a dist with a valid name, looks up in dist table
 * if found, fills in the missing parts of the structure
 */
int PINT_dist_lookup(PINT_dist *dist)
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
void PINT_dist_encode(void *buffer, PINT_dist *dist)
{
    PINT_dist* old_dist = dist;
	
    if (!dist)
        return;
    
    if (buffer)
    {
        memcpy(buffer, dist, PINT_DIST_PACK_SIZE(dist));
        dist = buffer;
        /* adjust pointers in new buffer */
        dist->dist_name = ((char *)dist + ((char *)old_dist->dist_name -
                                           (char *)old_dist));
        dist->params =
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
void PINT_dist_decode(PINT_dist *dist, void *buffer)
{
	if (!dist)
		return;
	if (buffer)
	{
		PINT_dist *d2 = (PINT_dist *)buffer;
		memcpy(dist, buffer, PINT_DIST_PACK_SIZE(d2));
	}
	/* convert ints in dist to pointers */
	dist->dist_name = (char *) dist + (unsigned long) dist->dist_name;
	dist->params = (void *) ((char *) dist + (unsigned long) dist->params);
	/* set methods */
	dist->methods = NULL;
	if (PINT_dist_lookup(dist)) {
	    gossip_err("%s: lookup dist failed\n", __func__);
	    exit(1);
	}
}

void PINT_dist_dump(PINT_dist *dist)
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

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
