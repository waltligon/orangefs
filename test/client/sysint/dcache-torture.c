/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>

#include <pint-dcache.h>
#include <gossip.h>

#define ENTRIES_TO_ADD 255

void gen_rand_str(int len, char** gen_str);

int main(int argc, char **argv)	
{
	int ret = -1;
	int found_flag, i;

	PVFS_pinode_reference test_ref;

	char* new_filename[ENTRIES_TO_ADD];

	PVFS_pinode_reference root_ref = {100,0};

	gossip_set_debug_mask(1, DCACHE_DEBUG);

	/* initialize the cache */
	ret = PINT_dcache_initialize();
	if(ret < 0)
	{
		fprintf(stderr, "dcache_initialize() failure.\n");
		return(-1);
	}

	for(i = 0; i < ENTRIES_TO_ADD; i++)
	{
		gen_rand_str( 10, &new_filename[i]);
		test_ref.handle = i;
		test_ref.fs_id = 0;
		ret = PINT_dcache_insert( new_filename[i], test_ref,
			root_ref);
		if(ret < 0)
		{
			fprintf(stderr, "Error: failed to insert entry.\n");
			return(-1);
		}
	}

	for(i = 0; i < ENTRIES_TO_ADD; i++)
	{
		ret = PINT_dcache_lookup(new_filename[i], root_ref, &test_ref);
		if(ret < 0)
		{
			fprintf(stderr, "dcache_lookup() failure.\n");
			return(-1);
		}

		if (i >= ENTRIES_TO_ADD - PINT_DCACHE_MAX_ENTRIES)
		{
			/*should have a valid handle*/
			if(test_ref.handle != i)
			{
				fprintf(stderr, "Failure: lookup returned %lld when it should have returned %d.\n", test_ref.handle, i);
			}
		}
		else
		{
			/*these should be cache misses*/
			if(test_ref.handle != PINT_DCACHE_HANDLE_INVALID)
			{
				fprintf(stderr, "Failure: lookup returned %lld when it shouldn't have returned a handle.\n", test_ref.handle);
			}
		}
	}

	/*remove all entries */
	for(i = 0; i < ENTRIES_TO_ADD;i++)
	{
		ret = PINT_dcache_remove(new_filename[i], root_ref,
			&found_flag);
		if(ret < 0)
		{
			fprintf(stderr, "Error: dcache_remove() failure.\n");
			return(-1);
		}

		if(!found_flag)
		{
			if (i >= ENTRIES_TO_ADD - PINT_DCACHE_MAX_ENTRIES)
			{
				/*should have a valid handle*/
				fprintf(stderr, "Error: dcache_remove() didn't find %d when it was supposed to.\n", i);
			}
		}
		else
		{
			if (i < ENTRIES_TO_ADD - PINT_DCACHE_MAX_ENTRIES)
			{
				/*shouldn't have a valid handle*/
				fprintf(stderr, "Error: dcache_remove() found %d when it wasn't supposed to.\n", i);
			}
		}
	}

	/* finalize the cache */
	ret = PINT_dcache_finalize();
	if(ret < 0)
	{
		fprintf(stderr, "dcache_finalize() failure.\n");
		return(-1);
	}

	/*free all the random filenames */
	for(i = 0; i < ENTRIES_TO_ADD;i++)
	{
		free(new_filename[i]);
	}

	return(0);
}

/* generate random filenames cause ddd sucks and doesn't like taking cmd line
 * arguments (and remove doesn't work yet so I can't cleanup the crap I already
 * created)
 */
void gen_rand_str(int len, char** gen_str)
{
        static char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
        int i;
        struct timeval poop;
        int newchar = 0;
        gettimeofday(&poop, NULL);

        *gen_str = malloc(len + 1);
        for(i = 0; i < len; i++)
        {
                newchar = ((1+(rand() % 26)) + poop.tv_usec) % 26;
                (*gen_str)[i] = alphabet[newchar];
        }
        (*gen_str)[len] = '\0';
}

