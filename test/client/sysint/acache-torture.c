/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#include "pvfs2-debug.h"
#include "acache.h"
#include "gossip.h"

#define ENTRIES_TO_ADD           512
#define DEFAULT_TIMEOUT_SECONDS    2

int main(int argc, char **argv)	
{
    int timeout = DEFAULT_TIMEOUT_SECONDS;
    int ret = -1, i = 0;
    int entries_to_add = 0;
    PINT_pinode *pinode1 = NULL, *pinode2 = NULL;
    PVFS_object_ref tmp;

    if (argc == 2)
    {
        sscanf(argv[1], "%d", &entries_to_add);
    }
    else
    {
        entries_to_add = ENTRIES_TO_ADD;
    }

    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_ACACHE_DEBUG);

    /* initialize the cache */
    ret = PINT_acache_initialize();
    if(ret < 0)
    {
        gossip_err("acache_initialize() failure.\n");
        return(-1);
    }

    PINT_acache_set_timeout(timeout * 1000);

    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode1 = PINT_acache_lookup(tmp, NULL, NULL);
        assert(pinode1 == NULL);

        pinode1 = PINT_acache_pinode_alloc();
        assert(pinode1);

        pinode1->refn = tmp;

        PINT_acache_set_valid(pinode1);
    }

    if (i == entries_to_add)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG, "Added %d entries to acache\n", i);
    }

    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode2 = PINT_acache_lookup(tmp, NULL, NULL);
        assert(pinode2);

        if (PINT_acache_pinode_status(pinode2, NULL) != PINODE_STATUS_VALID)
        {
            gossip_err("(1) Failure: lookup returned %Lu when it "
                       "should've returned %Lu.\n",
                       Lu(pinode2->refn.handle), Lu(tmp.handle));
        }
    }

    /* sleep to make sure all entries expired */
    gossip_debug(GOSSIP_ACACHE_DEBUG," Sleeping for %d seconds\n",timeout);
    sleep(timeout);

    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode2 = PINT_acache_lookup(tmp, NULL, NULL);
        assert(pinode2);

        if (PINT_acache_pinode_status(pinode2, NULL) == PINODE_STATUS_VALID)
        {
            gossip_err("(2) Failure: lookup returned %Lu when it "
                       "should've been expired.\n",
                       Lu(pinode2->refn.handle));
        }

        /* make them once again valid here before dropping the ref */
        PINT_acache_set_valid(pinode2);
        PINT_acache_release(pinode2);
    }

    /* again make sure entries are all valid */
    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        pinode2 = PINT_acache_lookup(tmp, NULL, NULL);
        assert(pinode2);

        if (PINT_acache_pinode_status(pinode2, NULL) != PINODE_STATUS_VALID)
        {
            gossip_err("(3) Failure: lookup returned %Lu when it "
                       "should've returned %Lu.\n",
                       Lu(pinode2->refn.handle), Lu(tmp.handle));
        }

        /*
          explicitly make all pinode entries invalid
          here (note, invalidate drops the ref)
        */
        PINT_acache_invalidate(tmp);
    }

    if (i == entries_to_add)
    {
        gossip_debug(GOSSIP_ACACHE_DEBUG, "All expected lookups were ok\n");
    }

    /* drop initial references */
    for(i = 0; i < entries_to_add; i++)
    {
        tmp.handle = (PVFS_handle)i;
        tmp.fs_id = (PVFS_fs_id)(i + 1000);

        PINT_acache_invalidate(tmp);
    }

    PINT_acache_finalize();

    return 0;
}
