/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>

char *enter_string(char *oldstring)
{
	char *newstring;
	newstring = (char *)emalloc((unsigned)strlen(oldstring)+1);
	strcpy(newstring, oldstring);
	return newstring;
}
