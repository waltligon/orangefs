/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pvfs2.h"
#include "pint-dcache.h"
#include "gossip.h"

#define ENTRIES_TO_ADD 255

int main(int argc, char **argv)	
{
    int ret = -1;
    int found_flag, i;
    PVFS_pinode_reference test_ref;
    char new_filename[ENTRIES_TO_ADD][PVFS_NAME_MAX];

    PVFS_pinode_reference root_ref = {100,0};

    gossip_enable_stderr();
    gossip_set_debug_mask(1, DCACHE_DEBUG);

    /* initialize the cache */
    ret = PINT_dcache_initialize();
    if(ret < 0)
    {
	gossip_err("dcache_initialize() failure.\n");
	return(-1);
    }

    PINT_dcache_set_timeout(5000);

    for(i = 0; i < ENTRIES_TO_ADD; i++)
    {
        snprintf(new_filename[i],PVFS_NAME_MAX,"dcache_testnameXXXXXX");
        mkstemp(new_filename[i]);
	test_ref.handle = i;
	test_ref.fs_id = 0;
	ret = PINT_dcache_insert(new_filename[i], test_ref, root_ref);
	if (ret < 0)
	{
	    gossip_err("Error: failed to insert entry.\n");
	    return(-1);
	}
    }

    gossip_debug(DCACHE_DEBUG, "Attempted insertion of %d dcache "
                 "elements\n", ENTRIES_TO_ADD);

    for(i = 0; i < ENTRIES_TO_ADD; i++)
    {
	ret = PINT_dcache_lookup(new_filename[i], root_ref, &test_ref);
	if ((ret < 0) && (ret != -PVFS_ENOENT))
	{
	    gossip_err("dcache_lookup() failure.\n");
	    return(-1);
	}

	if (i >= (ENTRIES_TO_ADD - PINT_DCACHE_MAX_ENTRIES))
	{
	    if (ret == -PVFS_ENOENT)
            {
		gossip_err("Failure: lookup didn't find an entry %d.\n",i);
                break;
	    }
	    /*should have a valid handle*/
	    else if (test_ref.handle != (PVFS_handle)i)
	    {
		gossip_err("Failure: lookup returned %Ld when it should "
                           "have returned %d.\n", test_ref.handle, i);
                break;
	    }
	}
	else
	{
	    /*these should be cache misses*/
	    if (ret == 0)
	    {
		gossip_err("Failure: lookup returned %Ld when it "
                           "shouldn't have returned a handle.\n",
                           test_ref.handle);
                break;
	    }
	}
    }

    if (i == ENTRIES_TO_ADD)
    {
        gossip_debug(DCACHE_DEBUG, "All expected lookups were ok\n");
    }

    /*remove all entries */
    for(i = 0; i < ENTRIES_TO_ADD; i++)
    {
	ret = PINT_dcache_remove(new_filename[i], root_ref, &found_flag);
	if (ret < 0)
	{
	    gossip_err("Error: dcache_remove() failure.\n");
	    return(-1);
	}

	if (!found_flag)
	{
	    if (i >= (ENTRIES_TO_ADD - PINT_DCACHE_MAX_ENTRIES))
	    {
                /*should have a valid handle*/
		gossip_err("Error: dcache_remove() didn't find %d when "
                           "it was supposed to.\n", i);
	    }
	}
	else
	{
	    if (i < (ENTRIES_TO_ADD - PINT_DCACHE_MAX_ENTRIES))
	    {
                /*shouldn't have a valid handle*/
		gossip_err("Error: dcache_remove() found %d when it "
                           "wasn't supposed to.\n", i);
	    }
	}
    }

    ret = PINT_dcache_finalize();
    if (ret < 0)
    {
	gossip_err("dcache_finalize() failure.\n");
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
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
