/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>

#include "gossip.h"
#include "ncache.h"

/* this is a test program that exercises the ncache interface and
 * demonstrates how to use it.
 */

int main(int argc, char **argv)        
{
    int ret = -1;
    int found_flag;

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

    PINT_ncache_set_timeout(5000);

    /* try to lookup something when there is nothing in there */
    ret = PINT_ncache_lookup("first", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             root_ref, &test_ref);
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
    ret = PINT_ncache_insert(first_name, PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             first_ref, root_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }
    ret = PINT_ncache_insert(second_name, PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             second_ref, first_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }
    ret = PINT_ncache_insert(third_name, PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             third_ref, second_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }

    /* lookup a few things */
    ret = PINT_ncache_lookup("first", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             root_ref, &test_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret == -PVFS_ENOENT || test_ref.handle != first_ref.handle)
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%Lu != %Lu)\n", Lu(test_ref.handle),
                Lu(first_ref.handle));
        return(-1);
    }

    ret = PINT_ncache_lookup("second", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             first_ref, &test_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }
    if (ret == -PVFS_ENOENT || test_ref.handle != second_ref.handle)
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%Lu != %Lu)\n", Lu(test_ref.handle),
                Lu(second_ref.handle));
        return(-1);
    }

    ret = PINT_ncache_lookup("third", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             second_ref, &test_ref);
    if (ret < 0 && ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }

    if ((ret == -PVFS_ENOENT) || (test_ref.handle != third_ref.handle))
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%Lu != %Lu)\n", Lu(test_ref.handle),
                Lu(third_ref.handle));
        return(-1);
    }

    /* sleep a little bit to let entries expire, then retry */
    printf("sleeping a few seconds before trying cache again.\n");
    sleep(7);

    ret = PINT_ncache_lookup("second", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             first_ref, &test_ref);
    if ((ret < 0) && (ret != -PVFS_ENOENT))
    {
        fprintf(stderr, "ncache_lookup() failure.\n");
        return(-1);
    }

    if (ret != -PVFS_ENOENT)
    {
        fprintf(stderr, "Failure: lookup didn't return correct "
                "handle (%Lu != %Lu)\n", Lu(test_ref.handle),
                Lu(first_ref.handle));
        fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
        return(-1);
    }

    ret = PINT_ncache_lookup("first", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             root_ref, &test_ref);
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

    ret = PINT_ncache_lookup("third", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             second_ref, &test_ref);
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
    ret = PINT_ncache_insert(first_name, PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             first_ref, root_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }
    ret = PINT_ncache_insert(first_name, PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             first_ref, root_ref);
    if(ret < 0)
    {
        fprintf(stderr, "Error: failed to insert entry.\n");
        return(-1);
    }

    /* then remove once */
    ret = PINT_ncache_remove(first_name, PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             root_ref, &found_flag);
    if(ret < 0)
    {
        fprintf(stderr, "Error: ncache_remove() failure.\n");
        return(-1);
    }
    if(!found_flag)
    {
        fprintf(stderr, "Failure: remove didn't find correct entry.\n");
        return(-1);
    }

    /* lookup the same entry, shouldn't get it */
    ret = PINT_ncache_lookup("first", PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             root_ref, &test_ref);
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
    ret = PINT_ncache_remove(first_name, PVFS2_LOOKUP_LINK_NO_FOLLOW,
                             root_ref, &found_flag);
    if(ret < 0)
    {
        fprintf(stderr, "Error: ncache_remove() failure.\n");
        return(-1);
    }
    if(found_flag)
    {
        fprintf(stderr, "Failure: remove found an entry it shouldn't have.\n");
        return(-1);
    }

    /* finalize the cache */
    ret = PINT_ncache_finalize();
    if(ret < 0)
    {
        fprintf(stderr, "ncache_finalize() failure.\n");
        return(-1);
    }

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
