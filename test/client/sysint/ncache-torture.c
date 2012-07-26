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
#define LESS_ENTRIES_TO_ADD 120

int main(int argc, char **argv)
{
    int ret = -1;
    int i,num_dirs,num_files;
    PVFS_object_ref test_ref;
    char new_filename[ENTRIES_TO_ADD][PVFS_NAME_MAX];
    char* filename;
    PVFS_object_ref root_ref = {100, 200};
    PVFS_object_ref parent_ref;
    PVFS_object_ref entry_ref;
    char entry_handle[1024];
    char entry_name[PVFS_NAME_MAX] = "";

    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_NCACHE_DEBUG);

    /* initialize the cache */
    ret = PINT_ncache_initialize();
    if(ret < 0)
    {
	gossip_err("ncache_initialize() failure.\n");
	return(-1);
    }

    PINT_ncache_set_info(TCACHE_TIMEOUT_MSECS,5000);
    PINT_ncache_set_info(TCACHE_HARD_LIMIT,ENTRIES_TO_ADD - 1);
    PINT_ncache_set_info(TCACHE_SOFT_LIMIT,LESS_ENTRIES_TO_ADD - 1);

    test_ref.handle = 1000;
    test_ref.fs_id = 2000;
    filename = "testfile1000";
    ret = PINT_ncache_update(
        (const char*) filename,
        (const PVFS_object_ref*) &test_ref, 
        (const PVFS_object_ref*) &root_ref);
    test_ref.handle = 1001;
    ret = PINT_ncache_get_cached_entry(
        (const char*) filename, 
        &test_ref,
        (const PVFS_object_ref*) &root_ref); 
    if (test_ref.handle != 1000 || ret != 0)
    {
        gossip_err("[1] Cannot properly resolve inserted entry!\n");
        return -1;
    }

    test_ref.handle = 1001;
    ret = PINT_ncache_update(
        (const char*) "testfile1001", 
        (const PVFS_object_ref*) &test_ref, 
        (const PVFS_object_ref*) &root_ref);

    ret = PINT_ncache_get_cached_entry(
        (const char*) "testfile1001", 
        &test_ref,
        (const PVFS_object_ref*) &root_ref); 
    if (test_ref.handle != 1001)
    {
        gossip_err("[2] Cannot properly resolve inserted entry!\n");
        return -1;
    }

    gossip_debug(GOSSIP_NCACHE_DEBUG, "Properly resolved two objects "
                 "with the\n  same name based on resolution type\n");

    sleep(2);

    /* Insert a bunch of entries into the cache */
    for(i = 1; i < ENTRIES_TO_ADD; i++)
    {
        snprintf(new_filename[i],PVFS_NAME_MAX,"ncache_testname%.3d",i);
	test_ref.handle = i;
	test_ref.fs_id = 0;
	ret = PINT_ncache_update(
            (const char*) new_filename[i],
            (const PVFS_object_ref*) &test_ref, 
            (const PVFS_object_ref*) &root_ref);
	if (ret < 0)
	{
	    gossip_err("Error: failed to insert entry.\n");
            PVFS_perror_gossip("return code is: ", ret);
	    return(-1);
	}
    }

    gossip_debug(GOSSIP_NCACHE_DEBUG, "Attempted insertion of %d ncache "
                 "elements\n", ENTRIES_TO_ADD);

    /* Make sure all entries can be retrieved */
    for(i = 1; i < ENTRIES_TO_ADD; i++)
    {
	ret = PINT_ncache_get_cached_entry(
            (const char*) new_filename[i], 
            &test_ref,
            (const PVFS_object_ref*) &root_ref); 
	if ((ret < 0) && (ret != -PVFS_ENOENT))
	{
	    gossip_err("ncache_get_cached_entry() failure.\n");
	    return(-1);
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
    for(i = 1; i < ENTRIES_TO_ADD; i++)
    {
        ret = PINT_ncache_get_cached_entry(
            (const char*) new_filename[i], 
            &test_ref,
            (const PVFS_object_ref*) &root_ref); 

        if (ret != -PVFS_ENOENT)
        {
            gossip_err("ncache_lookup() failure.\n");
            return(-1);
        }
    }

    /*remove all entries */
    for(i = 1; i < ENTRIES_TO_ADD; i++)
    {
	PINT_ncache_invalidate(
            (const char*) new_filename[i], 
            (const PVFS_object_ref*) &root_ref); 
    }

    PINT_ncache_set_info(TCACHE_TIMEOUT_MSECS,5000);
    PINT_ncache_set_info(TCACHE_HARD_LIMIT, 20000);
    PINT_ncache_set_info(TCACHE_SOFT_LIMIT, 10000);

    /* Stress Test */
    for(num_dirs=1;num_dirs<11;num_dirs++)
    {
        gossip_set_debug_mask(1, GOSSIP_NCACHE_DEBUG);
        gossip_debug(GOSSIP_NCACHE_DEBUG, "Adding entries for dir [%d]\n", num_dirs);
                
        gossip_set_debug_mask(0, GOSSIP_NCACHE_DEBUG);
        for(num_files=0;num_files<1000;num_files++)
        {
            snprintf(entry_name,PVFS_NAME_MAX,"child-dir%d",num_files);
            sprintf(entry_handle, "%d%d", num_dirs, num_files);
            entry_ref.handle = atoi(entry_handle); /* handle is [num_dirs][num_files] */
        	entry_ref.fs_id = 1175112761L;
            parent_ref.handle = 1879042325L + ((PVFS_handle) (num_dirs - 1));
            parent_ref.fs_id = 1175112761L;

        	ret = PINT_ncache_update(
        	    (const char*) entry_name,
                (const PVFS_object_ref*) &entry_ref, 
                (const PVFS_object_ref*) &parent_ref);
        }
    }

    PINT_ncache_finalize();
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
