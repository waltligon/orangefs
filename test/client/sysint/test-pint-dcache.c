/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>

#include <pint-dcache.h>
#include <gossip.h>

/* this is a test program that exercises the dcache interface and
 * demonstrates how to use it.
 */

/* create a static dcache to test here */
static struct dcache test_dcache;

int main(int argc, char **argv)	
{
	int ret = -1;
	int found_flag;

	pinode_reference test_ref;

	pinode_reference root_ref = {100,0};

	pinode_reference first_ref = {1,0};
	char first_name[] = "first";
	
	pinode_reference second_ref = {2,0};
	char second_name[] = "second";
	
	pinode_reference third_ref = {3,0};
	char third_name[] = "third";

	/* set debugging stuff */
	gossip_enable_stderr();
	gossip_set_debug_mask(1, DCACHE_DEBUG);


	/* initialize the cache */
	ret = dcache_initialize(&test_dcache);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_initialize() failure.\n");
		return(-1);
	}

	/* try to lookup something when there is nothing in there */
	ret = dcache_lookup(&test_dcache, "first", root_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != PINT_DCACHE_HANDLE_INVALID)
	{
		fprintf(stderr, "Failure: lookup succeeded when it shouldn't have.\n");
		return(-1);
	}

	/* insert a few things */
	ret = dcache_insert(&test_dcache, first_name, first_ref,
		root_ref);
	if(ret < 0)
	{
		fprintf(stderr, "Error: failed to insert entry.\n");
		return(-1);
	}
	ret = dcache_insert(&test_dcache, second_name, second_ref,
		first_ref);
	if(ret < 0)
	{
		fprintf(stderr, "Error: failed to insert entry.\n");
		return(-1);
	}
	ret = dcache_insert(&test_dcache, third_name, third_ref,
		second_ref);
	if(ret < 0)
	{
		fprintf(stderr, "Error: failed to insert entry.\n");
		return(-1);
	}

	/* lookup a few things */
	ret = dcache_lookup(&test_dcache, "first", root_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != first_ref.handle)
	{
		fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
		return(-1);
	}

	ret = dcache_lookup(&test_dcache, "second", first_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != second_ref.handle)
	{
		fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
		return(-1);
	}

	ret = dcache_lookup(&test_dcache, "third", second_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != third_ref.handle)
	{
		fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
		return(-1);
	}

	/* sleep a little bit to let entries expire, then retry */
	printf("sleeping a few seconds before trying cache again.\n");
	sleep(7);

	ret = dcache_lookup(&test_dcache, "second", first_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != PINT_DCACHE_HANDLE_INVALID)
	{
		fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
		return(-1);
	}

	ret = dcache_lookup(&test_dcache, "first", root_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != PINT_DCACHE_HANDLE_INVALID)
	{
		fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
		return(-1);
	}

	ret = dcache_lookup(&test_dcache, "third", second_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != PINT_DCACHE_HANDLE_INVALID)
	{
		fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
		return(-1);
	}

	/* try inserting twice */
	ret = dcache_insert(&test_dcache, first_name, first_ref,
		root_ref);
	if(ret < 0)
	{
		fprintf(stderr, "Error: failed to insert entry.\n");
		return(-1);
	}
	ret = dcache_insert(&test_dcache, first_name, first_ref,
		root_ref);
	if(ret < 0)
	{
		fprintf(stderr, "Error: failed to insert entry.\n");
		return(-1);
	}

	/* then remove once */
	ret = dcache_remove(&test_dcache, first_name, root_ref,
		&found_flag);
	if(ret < 0)
	{
		fprintf(stderr, "Error: dcache_remove() failure.\n");
		return(-1);
	}
	if(!found_flag)
	{
		fprintf(stderr, "Failure: remove didn't find correct entry.\n");
		return(-1);
	}

	/* lookup the same entry, shouldn't get it */
	ret = dcache_lookup(&test_dcache, "first", root_ref, &test_ref);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_lookup() failure.\n");
		return(-1);
	}
	if(test_ref.handle != PINT_DCACHE_HANDLE_INVALID)
	{
		fprintf(stderr, "Failure: lookup didn't return correct handle.\n");
		return(-1);
	}

	/* then remove again - found flag should be zero now */
	ret = dcache_remove(&test_dcache, first_name, root_ref,
		&found_flag);
	if(ret < 0)
	{
		fprintf(stderr, "Error: dcache_remove() failure.\n");
		return(-1);
	}
	if(found_flag)
	{
		fprintf(stderr, "Failure: remove found an entry it shouldn't have.\n");
		return(-1);
	}

	/* finalize the cache */
	ret = dcache_finalize(&test_dcache);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_finalize() failure.\n");
		return(-1);
	}

	return(0);
}
