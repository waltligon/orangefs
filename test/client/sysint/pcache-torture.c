/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include "pvfs2-debug.h"
#include "pcache.h"
#include "gossip.h"

#define ENTRIES_TO_ADD           512
#define DEFAULT_TIMEOUT_SECONDS    2

int main(int argc, char **argv)	
{
    int timeout = DEFAULT_TIMEOUT_SECONDS;
    int ret = -1, i = 0;
    int entries_to_add = 0;
    PINT_pinode *pinode1 = NULL, *pinode2 = NULL;
    PVFS_pinode_reference tmp;

    if (argc == 2)
    {
        sscanf(argv[1], "%d", &entries_to_add);
    }
    else
    {
        entries_to_add = ENTRIES_TO_ADD;
    }

    gossip_enable_stderr();
    gossip_set_debug_mask(1, PCACHE_DEBUG);

    /* initialize the cache */
    ret = PINT_pcache_initialize();
    if(ret < 0)
    {
        gossip_err("pcache_initialize() failure.\n");
        return(-1);
    }

    PINT_pcache_set_timeout(timeout * 1000);

    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode1 = PINT_pcache_lookup(tmp);
        assert(pinode1 == NULL);

        pinode1 = PINT_pcache_pinode_alloc();
        assert(pinode1);

        pinode1->refn = tmp;

        PINT_pcache_set_valid(pinode1);
    }

    if (i == entries_to_add)
    {
        gossip_debug(PCACHE_DEBUG, "Added %d entries to pcache\n", i);
    }

    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode2 = PINT_pcache_lookup(tmp);
        assert(pinode2);

        if (PINT_pcache_pinode_status(pinode2) != PINODE_STATUS_VALID)
        {
            gossip_err("(1) Failure: lookup returned %Ld when it "
                       "should've returned %Ld.\n",
                       pinode2->refn.handle, tmp.handle);
        }
    }

    /* sleep to make sure all entries expired */
    gossip_debug(PCACHE_DEBUG," Sleeping for %d seconds\n",timeout);
    sleep(timeout);

    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode2 = PINT_pcache_lookup(tmp);
        assert(pinode2);

        if (PINT_pcache_pinode_status(pinode2) == PINODE_STATUS_VALID)
        {
            gossip_err("(2) Failure: lookup returned %Ld when it "
                       "should've been expired.\n",
                       pinode2->refn.handle);
        }

        /* make them once again valid here before dropping the ref */
        PINT_pcache_set_valid(pinode2);
        PINT_pcache_release(pinode2);
    }

    /* again make sure entries are all valid */
    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode2 = PINT_pcache_lookup(tmp);
        assert(pinode2);

        if (PINT_pcache_pinode_status(pinode2) != PINODE_STATUS_VALID)
        {
            gossip_err("(3) Failure: lookup returned %Ld when it "
                       "should've returned %Ld.\n",
                       pinode2->refn.handle, tmp.handle);
        }

        /*
          explicitly make all pinode entries invalid
          here (note, invalidate drops the ref)
        */
        PINT_pcache_invalidate(tmp);
    }

    if (i == entries_to_add)
    {
        gossip_debug(PCACHE_DEBUG, "All expected lookups were ok\n");
    }

    /* drop initial references */
    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        PINT_pcache_release_refn(tmp);
    }

    PINT_pcache_finalize();

    return 0;
}
