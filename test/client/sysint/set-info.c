/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "client.h"
#include "gossip.h"

extern int parse_pvfstab(char *fn,pvfs_mntlist *mnt);

int main(int argc, char **argv)
{
    int ret = -1;
    char *filename = NULL;
    PVFS_fs_id fs_id;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_credentials credentials;
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_lookup resp_look;
    PVFS_pinode_reference pinode_refn;
    PVFS_sys_attr attr;

    gossip_enable_stderr();
    gossip_set_debug_mask(1,CLIENT_DEBUG);

    if (argc == 2)
    {
        filename = argv[1];
    }
    else
    {
        gossip_err("usage: %s /file_to_set_info_on\n", argv[0]);
        return ret;
    }

    ret = parse_pvfstab(NULL,&mnt);
    if (ret < 0)
    {
        gossip_err("Failed to parse pvfstab!\n");
        return ret;
    }

    ret = PVFS_sys_initialize(mnt, &resp_init);
    if(ret < 0)
    {
        gossip_err("PVFS_sys_initialize() failure. = %d\n", ret);
        return ret;
    }

    attr.owner = 100;
    attr.group = 100;
    attr.perms = (PVFS_U_WRITE | PVFS_U_READ);
    attr.atime = attr.ctime = attr.mtime = time(NULL);
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;

    credentials.uid = attr.owner;
    credentials.gid = attr.group;
    credentials.perms = attr.perms;

    fs_id = resp_init.fsid_list[0];

    gossip_ldebug(CLIENT_DEBUG,"about to lookup %s\n", filename);

    ret = PVFS_sys_lookup(fs_id, filename, credentials, &resp_look);
    if (ret < 0)
    {
        gossip_err("Lookup failed with errcode = %d\n", ret);
        return ret;
    }

    pinode_refn.handle = resp_look.pinode_refn.handle;
    pinode_refn.fs_id = fs_id;

    gossip_ldebug(CLIENT_DEBUG,"about to setattr on %s\n", filename);

    ret = PVFS_sys_setattr(pinode_refn, attr, credentials);
    if (ret < 0)
    {
        gossip_err("setattr failed with errcode = %d\n", ret);
        return ret;
    }
    else
    {
        gossip_ldebug(CLIENT_DEBUG,"setattr returned success\n");
    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        gossip_err("finalizing sysint failed with errcode = %d\n", ret);
        return ret;
    }

    gossip_disable();
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
