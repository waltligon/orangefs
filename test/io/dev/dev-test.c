/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdlib.h>

#include "gossip.h"
#include "pint-dev.h"

int main(int argc, char **argv)	
{
    int ret = 1;

    ret = PINT_dev_initialize("/dev/pvfs2-req", 0);
    if(ret < 0)
    {
	PVFS_perror("PINT_dev_initialize", ret);
	return(-1);
    }

    PINT_dev_finalize();

    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
