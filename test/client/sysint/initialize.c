/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <client.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

#include "pvfs2-util.h"

int main(int argc,char **argv)
{
	int ret = -1;

	ret = PVFS_util_init_defaults();
	if (ret < 0)
	{
		PVFS_perror("PVFS_util_init_defaults", ret);
		return (-1);
	}

	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	return(0);
}
