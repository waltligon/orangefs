/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>

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

	pinode_reference test_ref;

	pinode_reference root_ref = {100,0};

	pinode_reference first_ref = {1,0};
	char first_name[] = "first";
	
	pinode_reference second_ref = {2,0};
	char second_name[] = "second";
	
	pinode_reference third_ref = {3,0};
	char third_name[] = "third";
	

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

	/* TODO: check to see if the value that pops out of the dcache is
	 * valid or not
	 */

	/* finalize the cache */
	ret = dcache_finalize(&test_dcache);
	if(ret < 0)
	{
		fprintf(stderr, "dcache_finalize() failure.\n");
		return(-1);
	}

	return(0);
}
