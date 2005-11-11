/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pvfs2.h"
#include "ncache.h"
#include "gossip.h"
#include "pvfs2-internal.h"

#define ENTRIES_TO_ADD 255

int main(int argc, char **argv)
{
    int ret = -1;
    int found_flag, i;
    PVFS_object_ref test_ref;
    char new_filename[ENTRIES_TO_ADD][PVFS_NAME_MAX];

    PVFS_object_ref root_ref = {100, 200};

    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_NCACHE_DEBUG);

    /* initialize the cache */
    ret = PINT_ncache_initialize();
    if(ret < 0)
    {
	gossip_err("ncache_initialize() failure.\n");
	return(-1);
    }

    PINT_ncache_set_timeout(5000);

    test_ref.handle = 1000;
    test_ref.fs_id = 2000;

    ret = PINT_ncache_insert(
        "testfile001", PVFS2_LOOKUP_LINK_NO_FOLLOW,
        test_ref, root_ref);

    test_ref.handle = 1001;
    ret = PINT_ncache_insert(
        "testfile001", PVFS2_LOOKUP_LINK_FOLLOW,
        test_ref, root_ref);

    ret = PINT_ncache_lookup(
        "testfile001", PVFS2_LOOKUP_LINK_NO_FOLLOW,
        root_ref, &test_ref);
    if (test_ref.handle != 1000)
    {
        gossip_err("[1] Cannot properly resolve inserted entry!\n");
        return -1;
    }

    ret = PINT_ncache_lookup(
        "testfile001", PVFS2_LOOKUP_LINK_FOLLOW,
        root_ref, &test_ref);
    if (test_ref.handle != 1001)
    {
        gossip_err("[2] Cannot properly resolve inserted entry!\n");
        return -1;
    }

    gossip_debug(GOSSIP_NCACHE_DEBUG, "Properly resolved two objects "
                 "with the\n  same name based on resolution type\n");

    sleep(2);

    for(i = 0; i < ENTRIES_TO_ADD; i++)
    {
        snprintf(new_filename[i],PVFS_NAME_MAX,"ncache_testname%.3d",i);
	test_ref.handle = i;
	test_ref.fs_id = 0;
	ret = PINT_ncache_insert(
            new_filename[i], PVFS2_LOOKUP_LINK_NO_FOLLOW,
            test_ref, root_ref);
	if (ret < 0)
	{
	    gossip_err("Error: failed to insert entry.\n");
	    return(-1);
	}
    }

    gossip_debug(GOSSIP_NCACHE_DEBUG, "Attempted insertion of %d ncache "
                 "elements\n", ENTRIES_TO_ADD);

    for(i = 0; i < ENTRIES_TO_ADD; i++)
    {
	ret = PINT_ncache_lookup(
            new_filename[i], PVFS2_LOOKUP_LINK_NO_FOLLOW,
            root_ref, &test_ref);
	if ((ret < 0) && (ret != -PVFS_ENOENT))
	{
	    gossip_err("ncache_lookup() failure.\n");
	    return(-1);
	}

	if (i >= (ENTRIES_TO_ADD - PINT_NCACHE_MAX_ENTRIES))
	{
	    if (ret == -PVFS_ENOENT)
            {
		gossip_err("Failure: lookup didn't find an entry %d.\n",i);
                break;
	    }
	    /*should have a valid handle*/
	    else if (test_ref.handle != (PVFS_handle)i)
	    {
		gossip_err("Failure: lookup returned %llu when it should "
                           "have returned %d.\n", llu(test_ref.handle), i);
                break;
	    }
	}
	else
	{
	    /*these should be cache misses*/
	    if (ret == 0)
	    {
		gossip_err("Failure: lookup returned %llu when it "
                           "shouldn't have returned a handle.\n",
                           llu(test_ref.handle));
                break;
	    }
	}
    }

    if (i == ENTRIES_TO_ADD)
    {
        gossip_debug(GOSSIP_NCACHE_DEBUG, "All expected lookups were ok\n");
    }

    /* sleep to make sure everything expires */
    gossip_debug(GOSSIP_NCACHE_DEBUG,
                 "Waiting for all entries to expire\n");
    sleep(5);

    /* make sure all valid entries are now expired */
    for(i = 0; i < ENTRIES_TO_ADD; i++)
    {
        if (i < (ENTRIES_TO_ADD - PINT_NCACHE_MAX_ENTRIES))
        {
            ret = PINT_ncache_lookup(
                new_filename[i], PVFS2_LOOKUP_LINK_NO_FOLLOW,
                root_ref, &test_ref);

            if (ret != -PVFS_ENOENT)
            {
                gossip_err("ncache_lookup() failure.\n");
                return(-1);
            }
        }
    }

    /*remove all entries */
    for(i = 0; i < ENTRIES_TO_ADD; i++)
    {
	ret = PINT_ncache_remove(
            new_filename[i], PVFS2_LOOKUP_LINK_NO_FOLLOW,
            root_ref, &found_flag);
	if (ret < 0)
	{
	    gossip_err("Error: ncache_remove() failure.\n");
	    return(-1);
	}

	if (!found_flag)
	{
	    if (i >= (ENTRIES_TO_ADD - PINT_NCACHE_MAX_ENTRIES))
	    {
                /*should have a valid handle*/
		gossip_err("Error: ncache_remove() didn't find %d when "
                           "it was supposed to.\n", i);
	    }
	}
	else
	{
	    if (i < (ENTRIES_TO_ADD - PINT_NCACHE_MAX_ENTRIES))
	    {
                /*shouldn't have a valid handle*/
		gossip_err("Error: ncache_remove() found %d when it "
                           "wasn't supposed to.\n", i);
	    }
	}
    }

    ret = PINT_ncache_finalize();
    if (ret < 0)
    {
	gossip_err("ncache_finalize() failure.\n");
	return(-1);
    }
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
