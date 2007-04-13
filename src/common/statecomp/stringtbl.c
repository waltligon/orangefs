/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup statecomp
 *
 *  String storage routine for statecomp.
 *
 *  \todo Merge into statecomp.c.
 */

#include <stdio.h>
#include <string.h>

void *emalloc(unsigned int size);
char *enter_string(char *oldstring);

char *enter_string(char *oldstring)
{
    char *newstring;
    newstring = (char *)emalloc((unsigned)strlen(oldstring)+1);
    strcpy(newstring, oldstring);
    return newstring;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
