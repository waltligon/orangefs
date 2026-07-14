/*
 * (C) 2002 Clemson University.
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2-types.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "internal.h"
#include "pint-distribution.h"

/* global size of dist table */
#define PINT_DIST_TABLE_SZ 8
static int PINT_Dist_count = 0; 

/* compiled-in distributions */
extern PINT_dist basic_dist;
extern PINT_dist simple_stripe_dist;

/* initial dist table - default dists */
static PINT_dist *PINT_Dist_table[PINT_DIST_TABLE_SZ] = {0};

/*
 * add a dist to dist table
 */
int PINT_register_distribution(PINT_dist *d_p)
{
    if (0 != d_p && PINT_Dist_count < PINT_DIST_TABLE_SZ)
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
    {
        return -1;
    }
    for (d = 0; d < PINT_Dist_count && PINT_Dist_table[d]; d++)
    {
        if (strncmp(dist_name, PINT_Dist_table[d]->dist_name,
                    PINT_DIST_NAME_SZ) == 0)
        {
            PINT_Dist_table[d]->methods->unregister();
            /* bubble up */
            --PINT_Dist_count;
            for (; d<PINT_Dist_count; d++)
            {
                PINT_Dist_table[d] = PINT_Dist_table[d+1];
            }
            /* clean out the last entry we just moved up */
            PINT_Dist_table[d] = NULL;
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
/*
 * mallocing PINT_DIST_PACK_SIZE allocates enough memory for the PINT_dist
 * struct and the name string and parameters specific to the distribution.
 * sizes are rounded up by 8 to ensure each starts on an even 64-bit boundary.
 */
PINT_dist *PINT_dist_create(const char *name)
{
    PINT_dist old_dist;
    PINT_dist *new_dist = 0;

    gossip_ldebug(GOSSIP_DIST_DEBUG, "Creating new Dist from table\n");

    if (!name)
    {
        return 0;
    }
    old_dist.dist_name = (char*)name;
    old_dist.params = 0;
    old_dist.methods = 0;
    if (PINT_dist_lookup(&old_dist) == 0)
    {
        /* distribution was found */
        gossip_ldebug(GOSSIP_DIST_DEBUG, "New Dist name found in table\n");
        new_dist = malloc(PINT_DIST_PACK_SIZE(&old_dist));
        if (new_dist)
        {
            /* copy all fields */
            gossip_ldebug(GOSSIP_DIST_DEBUG, "Copying all fields\n");
            *new_dist = old_dist;
            /* find address for name and params */
            new_dist->dist_name
                    = (char *) new_dist + roundup8(sizeof(*new_dist));
            new_dist->params
                = (void *)(new_dist->dist_name + roundup8(new_dist->name_size));
            /* after lookup there must be enough room to hold name */
            assert(old_dist.name_size >= (strlen(old_dist.dist_name) + 1));
            /* copy using length of string passed in by caller
             * rather than rounded up name_size used for distribution packing
             * NOTE, I'm not sure why use this and not a strcpy, or a
             * fixed size memcpy.  WBL
             */
            memcpy(new_dist->dist_name, old_dist.dist_name,
                                        (strlen(old_dist.dist_name) + 1));
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

/* copies an entire dist in binary to a new contiguous block of memory */
PINT_dist *PINT_dist_copy(PINT_dist **dist, const PINT_dist *sdist)
{
    int dist_size;

    if (dist == NULL)
    {
        gossip_lerr("destination distribution pointer is NULL\n");
        return NULL;
    }

    if (sdist == NULL)
    {
        gossip_lerr("source distribution pointer is NULL\n");
        return NULL;
    }

    gossip_ldebug(GOSSIP_COMMON_DEBUG, "Starting distribution copy (%p)->(%p)\n", sdist, *dist);

    dist_size = PINT_DIST_PACK_SIZE(sdist);
    gossip_ldebug(GOSSIP_COMMON_DEBUG, "Dist size %d\n", dist_size);
    if (*dist == NULL)
    {
        (*dist) = (PINT_dist *)malloc(dist_size);
        gossip_ldebug(GOSSIP_COMMON_DEBUG, "Mallocing dest (%p)\n", (*dist));
    }
    if (*dist)
    {
        gossip_ldebug(GOSSIP_COMMON_DEBUG, "Copying (%p)->(%p)\n", sdist, (*dist));
        memcpy((*dist), sdist, dist_size);
        /* fixup pointers to new space */
        (*dist)->dist_name
                      = (char *) (*dist) + roundup8(sizeof(**dist));
        (*dist)->params
                = (void *)((*dist)->dist_name + roundup8((*dist)->name_size));
        memcpy((*dist)->dist_name, sdist->dist_name, sdist->name_size);
        memcpy((*dist)->params, sdist->params, sdist->param_size);
    }
    return (*dist);
}

/* copies a binary block of parameters from a dist */
int PINT_dist_getparams(void *buf, const PINT_dist *dist)
{
    if (!dist || !buf)
    {
        return -1;
    }
    memcpy (buf, dist->params, dist->param_size);
    return 0;
}

/* copies a binary block of parameters into a dist */
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
    {
        return -1;
    }
    for (d = 0; d < PINT_Dist_count && PINT_Dist_table[d]; d++)
    {
        if (!strncmp(dist->dist_name, PINT_Dist_table[d]->dist_name,
                    PINT_DIST_NAME_SZ))
        {
            dist->name_size = PINT_Dist_table[d]->name_size;
            dist->param_size = PINT_Dist_table[d]->param_size;
            if (!dist->params)
            {
                dist->params = PINT_Dist_table[d]->params;
            }
            if (!dist->methods)
            {
                dist->methods = PINT_Dist_table[d]->methods;
            }
            return 0; /* success */
        }
    }
    return -1;
}

/* again, why are we doing this? */
/* pack dist struct for storage from dist to buffer */
void PINT_dist_encode(void *buffer, PINT_dist *dist)
{
    char *tmpbuf = (char *)buffer;
    encode_PINT_dist(&tmpbuf, &dist);
}

/* unpack dist struct after receiving from storage from buffer to dist */
void PINT_dist_decode(PINT_dist **dist, void *buffer)
{
    char *tmpbuf = (char *)buffer;
    decode_PINT_dist(&tmpbuf, dist, NULL);
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
