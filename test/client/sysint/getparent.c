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
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_getparent resp_getparent;
    int ret = -1;
    PVFS_util_tab mnt = {0,NULL};
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;

    if (argc != 2)
    {
        printf("USAGE: %s /path/to/lookup\n", argv[0]);
        return 1;
    }

    printf("lookup up path %s\n", argv[1]);

    ret = PVFS_util_parse_pvfstab(NULL, &mnt);
    if (ret < 0)
    {
        printf("Parsing error\n");
        return(-1);
    }

    ret = PVFS_sys_initialize(mnt, GOSSIP_CLIENT_DEBUG, &resp_init);
    if(ret < 0)
    {
        printf("PVFS_sys_initialize() failure. = %d\n", ret);
        return(ret);
    }

    credentials.uid = getuid();
    credentials.gid = getgid();

    fs_id = resp_init.fsid_list[0];

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
