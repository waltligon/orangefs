/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pvfs2-types.h"
#include "gossip.h"

/* PVFS_perror()
 *
 * prints a message on stderr, consisting of text argument
 * followed by a colon, space, and error string for the given
 * retcode.
 * NOTE: also prints a warning if the error code is not in pvfs2
 * format and assumes errno
 *
 * no return value
 */
void PVFS_perror(char* text, int retcode)
{
    if(IS_PVFS_ERROR(-retcode))
    {
	fprintf(stderr, "%s: %s\n", text,
	strerror(PVFS_ERROR_TO_ERRNO(-retcode)));
	/* TODO: probably we should do something to print
	 * out the class too?
	 */
    }       
    else
    {
	fprintf(stderr, "Warning: non PVFS2 error code:\n");
	fprintf(stderr, "%s: %s\n", text,
	strerror(-retcode));
    }
    return;
}

/* PVFS_perror_gossip()
 *
 * same as PVFS_perror, except that the output is routed through 
 * gossip rather than stderr
 *
 * no return value
 */
void PVFS_perror_gossip(char* text, int retcode)
{
    if(IS_PVFS_ERROR(-retcode))
    {
	gossip_err("%s: %s\n", text, strerror(PVFS_ERROR_TO_ERRNO(-retcode)));
	/* TODO: probably we should do something to print
	 * out the class too?
	 */
    }       
    else
    {
	gossip_err("Warning: non PVFS2 error code:\n");
	gossip_err("%s: %s\n", text, strerror(-retcode));
    }
    return;
}

/* macro defined in include/pvfs2-types.h */
DECLARE_ERRNO_MAPPING_AND_FN();


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
