/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>

#include <pcache.h>
#include <gossip.h>

#define ENTRIES_TO_ADD 60

int main(int argc, char **argv)	
{
	int ret = -1;
	int i;
	int entries_to_add;
	pinode *pinode1, *pinode2, *pinode3;
	pinode_reference test_ref;

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
		fprintf(stderr, "pcache_initialize() failure.\n");
		return(-1);
	}

	for(i = 0; i < entries_to_add; i++)
	{
		/* alloc the new pinode */
		ret = PINT_pcache_pinode_alloc( &pinode1 );
		if(ret < 0)
		{
			fprintf(stderr, "Error: failed to insert entry.\n");
			return(-1);
		}
		pinode1->pinode_ref.handle = i;
		pinode1->pinode_ref.fs_id = i+1000;
		ret = PINT_pcache_insert( pinode1 );
		if( ret < 0 )
		{
			fprintf(stderr, "Error: failed to insert entry %d.\n",i);
			PINT_pcache_pinode_dealloc( pinode1 );
		}
		ret = PINT_pcache_insert_rls( pinode1 );
		if( ret < 0 )
		{
			fprintf(stderr, "Error: insert_rls failed %d.\n",i);
		}
		pinode1 = NULL;
	}

	for(i = 0; i < entries_to_add; i++)
	{
		test_ref.handle = i;
		test_ref.fs_id = i + 1000;
		ret = PINT_pcache_lookup(test_ref, &pinode2);
		if(ret == PCACHE_LOOKUP_FAILURE)
		{
			if (i > entries_to_add - PINT_PCACHE_MAX_ENTRIES)
			{
				/*should have a valid handle*/
				fprintf(stderr, "Failure: lookup didn't return anything when it should have returned the pinode for %lld.\n", test_ref.handle);
			}
		}
		else
		{
			if (i >= entries_to_add - PINT_PCACHE_MAX_ENTRIES)
			{
				if (test_ref.handle != pinode2->pinode_ref.handle)
				{
					fprintf(stderr, "Failure: lookup returned %lld when it should've returned %lld.\n", pinode2->pinode_ref.handle, test_ref.handle );
				}
			}
			else
			{
				/*these should be cache misses*/
				fprintf(stderr, "Failure: lookup returned %lld when it shouldn't have returned anything (iter %d).\n", pinode2->pinode_ref.handle, i);
			}
			ret = PINT_pcache_lookup_rls(pinode2);
			if( ret < 0 )
			{
				fprintf(stderr, "Error: lookup_rls failed %d.\n",i);
			}
		}
	}

	/*remove all entries */
	for(i = 0; i < entries_to_add;i++)
	{
		test_ref.handle = i;
		test_ref.fs_id = i + 1000;
		ret = PINT_pcache_remove(test_ref, &pinode3);
		if(ret < 0)
		{
			fprintf(stderr, "Error: pcache_remove() did not find %d in the cache.\n", i);
			continue;
		}
		PINT_pcache_pinode_dealloc(pinode3);
		
	}

	/* finalize the cache */
	ret = PINT_pcache_finalize();
	if(ret < 0)
	{
		fprintf(stderr, "pcache_finalize() failure.\n");
		return(-1);
	}
	return(0);
}
