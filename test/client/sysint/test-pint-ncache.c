/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>

#include "pvfs2.h"
#include "ncache.h"
#include "gossip.h"
#include "pvfs2-internal.h"

/* this is a test program that exercises the ncache interface and
 * demonstrates how to use it.
 */

int main(int argc, char **argv)        
{
    int ret = -1;

    PVFS_object_ref test_ref;

    PVFS_object_ref root_ref = {100,0};

    PVFS_object_ref first_ref = {1,0};
    char first_name[] = "first";
        
    PVFS_object_ref second_ref = {2,0};
    char second_name[] = "second";
        
    PVFS_object_ref third_ref = {3,0};
    char third_name[] = "third";

    /* set debugging stuff */
    gossip_enable_stderr();
    gossip_set_debug_mask(1, GOSSIP_NCACHE_DEBUG);

    /* initialize the cache */
    ret = PINT_ncache_initialize();
    if(ret < 0)
    {
        fprintf(stderr, "ncache_initialize() failure.\n");
        return(-1);
    }

    PINT_ncache_set_info(TCACHE_TIMEOUT_MSECS,5000);

    /* try to lookup something when there is nothing in there */
    ret = PINT_ncache_get_cached_entry((const char*) "first", 
                                       &test_ref, 
                                       (const PVFS_object_ref*) &root_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret == 0)
    {
        fprintf(stderr, "Failure: lookup succeeded when it shouldn't have.\n");
        return(-1);
    }

    /* insert a few things */
    ret = PINT_ncache_update((const char*) first_name, 
                             (const PVFS_object_ref*) &first_ref, 
                             (const PVFS_object_ref*) &root_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }
    ret = PINT_ncache_update((const char*) second_name, 
                             (const PVFS_object_ref*) &second_ref, 
                             (const PVFS_object_ref*) &first_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }
    ret = PINT_ncache_update((const char*) third_name, 
                             (const PVFS_object_ref*) &third_ref, 
                             (const PVFS_object_ref*) &second_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }

    /* lookup a few things */
    ret = PINT_ncache_get_cached_entry((const char*) "first",
                                       &test_ref,
                                       (const PVFS_object_ref*) &root_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret == -PVFS_ENOENT || test_ref.handle != first_ref.handle)
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%llu != %llu)\n", llu(test_ref.handle),
                llu(first_ref.handle));
        return(-1);
    }

    ret = PINT_ncache_get_cached_entry((const char*) "second", 
                                       &test_ref,
                                       (const PVFS_object_ref*) &first_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret == -PVFS_ENOENT || test_ref.handle != second_ref.handle)
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%llu != %llu)\n", llu(test_ref.handle),
                llu(second_ref.handle));
        return(-1);
    }

    ret = PINT_ncache_get_cached_entry((const char*) "third", 
                                       &test_ref,
                                       (const PVFS_object_ref*) &second_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }

    if ((ret == -PVFS_ENOENT) || (test_ref.handle != third_ref.handle))
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%llu != %llu)\n", llu(test_ref.handle),
                llu(third_ref.handle));
        return(-1);
    }

    /* sleep a little bit to let entries expire, then retry */
    printf("sleeping a few seconds before trying cache again.\n");
    sleep(7);

    ret = PINT_ncache_get_cached_entry((const char*) "second", 
                                       &test_ref,
                                       (const PVFS_object_ref*) &first_ref);
    if ((ret < 0) && (ret != -PVFS_ENOENT))
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }

    if (ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%llu != %llu)\n", llu(test_ref.handle),
                llu(first_ref.handle));
        fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
        return(-1);
    }

    ret = PINT_ncache_get_cached_entry((const char*) "first",
                                        &test_ref,
                                        (const PVFS_object_ref*) &root_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
        return(-1);
    }

    ret = PINT_ncache_get_cached_entry((const char*) "third", 
                                       &test_ref,
                                       (const PVFS_object_ref*) &second_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
        return(-1);
    }

    /* try inserting twice */
    ret = PINT_ncache_update((const char*) first_name, 
                             (const PVFS_object_ref*) &first_ref, 
                             (const PVFS_object_ref*) &root_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }
    ret = PINT_ncache_update(first_name, 
                             (const PVFS_object_ref*) &first_ref, 
                             (const PVFS_object_ref*) &root_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }

    /* then remove once */
    PINT_ncache_invalidate((const char*) first_name,
                           (const PVFS_object_ref*) &root_ref);

    /* lookup the same entry, shouldn't get it */
    ret = PINT_ncache_get_cached_entry((const char*) "first", 
                                       &test_ref,
                                       (const PVFS_object_ref*) &root_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
        return(-1);
    }

    /* then remove again - found flag should be zero now */
    PINT_ncache_invalidate((const char*) first_name,
                           (const PVFS_object_ref*) &root_ref);

    /* finalize the cache */
    PINT_ncache_finalize();
    gossip_disable();

    return(0);
}

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
