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
	PVFS_sysresp_init resp_init;
	int ret = -1;
	const PVFS_util_tab* tab;

	/* Parse PVFStab */
	tab = PVFS_util_parse_pvfstab(NULL);
	if (!tab)
	{
		printf("Parsing error\n");
		return(-1);
	}

	/*Init the system interface*/
	ret = PVFS_sys_initialize(*tab, GOSSIP_CLIENT_DEBUG, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}

	/*close it down*/
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	return(0);
}
