/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>

void *malloc();

/*
 *	emalloc --- error checking malloc routine
 */
void * emalloc(unsigned int size)
{
	void *p;


	if (!(p = malloc(size)))
	{
		fprintf(stderr,"no more dynamic storage - aborting\n");
		exit(1);
	}

	return(p);
}
