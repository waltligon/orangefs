/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 *  Malloc function with error handling capability, used in statecomp.
 *
 *  \todo Merge into statecomp.c or something.
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

void *emalloc(unsigned int size);

/** error checking malloc routine
 */
void *emalloc(unsigned int size)
{
    void *p;


    if (!(p = malloc(size)))
    {
	fprintf(stderr,"no more dynamic storage - aborting\n");
	exit(1);
    }

    return(p);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
