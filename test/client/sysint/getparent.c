/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "client.h"
#include "pvfs2-util.h"

int main(int argc,char **argv)
{
    PVFS_sysresp_getparent resp_getparent;
    int ret = -1;
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;

    if (argc != 2)
    {
        printf("USAGE: %s /path/to/lookup\n", argv[0]);
        return 1;
    }

    printf("lookup up path %s\n", argv[1]);

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }
    ret = PVFS_util_get_default_fsid(&fs_id);
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", ret);
	return (-1);
    }

    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_sys_getparent(fs_id, argv[1], credentials, &resp_getparent);
    if (ret == 0)
    {
        printf("=== getparent data:\n");
        printf("resp_getparent.basename: %s\n",
               resp_getparent.basename);
        printf("resp_getparent.parent_refn.fs_id: %d\n",
               resp_getparent.parent_refn.fs_id);
        printf("resp_getparent.parent_refn.handle: %Ld\n",
               Ld(resp_getparent.parent_refn.handle));
    }
    else
    {
        PVFS_perror("getparent failed ", ret);
    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        printf("finalizing sysint failed with errcode = %d\n", ret);
        return (-1);
    }
    return(0);
}
