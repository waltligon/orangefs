/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <sys/time.h>
#include <stdio.h>

#include "client.h"
#include "pvfs2-util.h"

void gen_rand_str(int len, char** gen_str);

int main(int argc,char **argv)
{
	PVFS_sysresp_init resp_init;
	PVFS_sysresp_getparent resp_getparent;
	int ret = -1;
	pvfs_mntlist mnt = {0,NULL};
	PVFS_fs_id fs_id;
	PVFS_credentials credentials;

	if (argc != 2)
	{
		printf("USAGE: %s /path/to/lookup\n", argv[0]);
                return 1;
	}

	printf("lookup up path %s\n", argv[1]);

	/* Parse PVFStab */
	ret = PVFS_util_parse_pvfstab(NULL,&mnt);
	if (ret < 0)
	{
		printf("Parsing error\n");
		return(-1);
	}
	/*Init the system interface*/
	ret = PVFS_sys_initialize(mnt, CLIENT_DEBUG, &resp_init);
	if(ret < 0)
	{
		printf("PVFS_sys_initialize() failure. = %d\n", ret);
		return(ret);
	}
	printf("SYSTEM INTERFACE INITIALIZED\n");

	credentials.uid = 100;
	credentials.gid = 100;

	fs_id = resp_init.fsid_list[0];

	ret = PVFS_sys_getparent(fs_id, argv[1], credentials, &resp_getparent);
	printf("=== getparent data:\n");
	printf("resp_getparent.basename: %s\n", resp_getparent.basename);
	printf("resp_getparent.parent_refn.fs_id: %d\n", resp_getparent.parent_refn.fs_id);
	printf("resp_getparent.parent_refn.handle: %Ld\n", resp_getparent.parent_refn.handle);

	//close it down
	ret = PVFS_sys_finalize();
	if (ret < 0)
	{
		printf("finalizing sysint failed with errcode = %d\n", ret);
		return (-1);
	}

	return(0);
}
