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
#include "pvfs2-util.h"

int main(int argc, char **argv)
{
    int ret = -1;
    char *filename = NULL;
    PVFS_fs_id fs_id;
    pvfs_mntlist mnt = {0,NULL};
    PVFS_credentials credentials;
    PVFS_sysresp_init resp_init;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_pinode_reference pinode_refn;

    if (argc == 2)
    {
        filename = argv[1];
    }
    else
    {
        fprintf(stderr, "usage: %s /file_to_set_info_on\n", argv[0]);
        return ret;
    }

    ret = PVFS_util_parse_pvfstab(&mnt);
    if (ret < 0)
    {
        fprintf(stderr, "Failed to parse pvfstab!\n");
        return ret;
    }

    ret = PVFS_sys_initialize(mnt, CLIENT_DEBUG, &resp_init);
    if(ret < 0)
    {
        fprintf(stderr, "PVFS_sys_initialize() failure. = %d\n", ret);
        return ret;
    }

    /* fake credentials here */
    credentials.uid = 100;
    credentials.gid = 100;

    fs_id = resp_init.fsid_list[0];

    printf("about to lookup %s\n", filename);

    ret = PVFS_sys_lookup(fs_id, filename, credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW);
    if (ret < 0)
    {
        fprintf(stderr, "Lookup failed with errcode = %d\n", ret);
        return ret;
    }

    pinode_refn.handle = resp_look.pinode_refn.handle;
    pinode_refn.fs_id = fs_id;

    printf("about to getattr on %s\n", filename);

    ret = PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL_SETABLE,
                           credentials, &resp_getattr);
    if (ret < 0)
    {
        printf("getattr failed with errcode = %d\n", ret);
        return ret;
    }

    printf("Retrieved the following attributes\n");
    printf("Handle      : %Lu\n", Lu(pinode_refn.handle));
    printf("FSID        : %d\n", (int)pinode_refn.fs_id);
    printf("mask        : %d\n", resp_getattr.attr.mask);
    printf("uid         : %d\n", resp_getattr.attr.owner);
    printf("gid         : %d\n", resp_getattr.attr.group);
    printf("permissions : %d\n", resp_getattr.attr.perms);
    printf("atime       : %s", ctime((time_t *)&resp_getattr.attr.atime));
    printf("mtime       : %s", ctime((time_t *)&resp_getattr.attr.mtime));
    printf("ctime       : %s", ctime((time_t *)&resp_getattr.attr.ctime));

    /* take the retrieved attributes and update the access time */
    resp_getattr.attr.atime = time(NULL);
    resp_getattr.attr.mask &= ~PVFS_ATTR_COMMON_TYPE;
    /*
      explicitly set the PVFS_ATTR_COMMON_ATIME, since we
      want to update the atime field in particular
    */
    resp_getattr.attr.mask |= PVFS_ATTR_COMMON_ATIME;

    /* use stored credentials here */
    credentials.uid = resp_getattr.attr.owner;
    credentials.gid = resp_getattr.attr.group;

    printf("about to setattr on %s\n", filename);

    ret = PVFS_sys_setattr(pinode_refn, resp_getattr.attr, credentials);
    if (ret < 0)
    {
        fprintf(stderr, "setattr failed with errcode = %d\n", ret);
        return ret;
    }
    else
    {
        printf("setattr returned success\n");

        printf("Set the following attributes\n");
        printf("Handle      : %Lu\n", Lu(pinode_refn.handle));
        printf("FSID        : %d\n", (int)pinode_refn.fs_id);
        printf("mask        : %d\n", resp_getattr.attr.mask);
        printf("uid         : %d\n", resp_getattr.attr.owner);
        printf("gid         : %d\n", resp_getattr.attr.group);
        printf("permissions : %d\n", resp_getattr.attr.perms);
        printf("atime       : %s", ctime((time_t *)&resp_getattr.attr.atime));
        printf("mtime       : %s", ctime((time_t *)&resp_getattr.attr.mtime));
        printf("ctime       : %s", ctime((time_t *)&resp_getattr.attr.ctime));
    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
        fprintf(stderr, "finalizing sysint failed with errcode = %d\n", ret);
        return ret;
    }

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
