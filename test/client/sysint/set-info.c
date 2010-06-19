/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "client.h"
#include "pvfs2-util.h"
#include "pvfs2-internal.h"

int main(int argc, char **argv)
{
    int ret = -1;
    char *filename = NULL;
    PVFS_fs_id fs_id;
    PVFS_credentials credentials;
    PVFS_sysresp_lookup resp_look;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_object_ref pinode_refn;
    time_t r_atime, r_mtime, r_ctime;

    if (argc == 2)
    {
        filename = argv[1];
    }
    else
    {
        fprintf(stderr, "usage: %s /file_to_set_info_on\n", argv[0]);
        return ret;
    }

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

    printf("about to lookup %s\n", filename);

    ret = PVFS_sys_lookup(fs_id, filename, &credentials,
                          &resp_look, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "Lookup failed with errcode = %d\n", ret);
        return ret;
    }

    pinode_refn.handle = resp_look.ref.handle;
    pinode_refn.fs_id = fs_id;

    printf("about to getattr on %s\n", filename);

    ret = PVFS_sys_getattr(pinode_refn, PVFS_ATTR_SYS_ALL_SETABLE,
                           &credentials, &resp_getattr, NULL);
    if (ret < 0)
    {
        printf("getattr failed with errcode = %d\n", ret);
        return ret;
    }

    r_atime = (time_t)resp_getattr.attr.atime;
    r_mtime = (time_t)resp_getattr.attr.mtime;
    r_ctime = (time_t)resp_getattr.attr.ctime;

    printf("Retrieved the following attributes\n");
    printf("Handle      : %llu\n", llu(pinode_refn.handle));
    printf("FSID        : %d\n", (int)pinode_refn.fs_id);
    printf("mask        : %d\n", resp_getattr.attr.mask);
    printf("uid         : %d\n", resp_getattr.attr.owner);
    printf("gid         : %d\n", resp_getattr.attr.group);
    printf("permissions : %d\n", resp_getattr.attr.perms);
    printf("atime       : %s", ctime(&r_atime));
    printf("mtime       : %s", ctime(&r_mtime));
    printf("ctime       : %s", ctime(&r_ctime));

    /* take the retrieved attributes and update the modification time */
    resp_getattr.attr.mtime = time(NULL);
    resp_getattr.attr.mask &= ~PVFS_ATTR_SYS_TYPE;
    /*
      explicitly set the PVFS_ATTR_COMMON_ATIME, since we
      want to update the atime field in particular
    */
    resp_getattr.attr.mask |= PVFS_ATTR_SYS_ATIME;

    /* use stored credentials here */
    credentials.uid = resp_getattr.attr.owner;
    credentials.gid = resp_getattr.attr.group;

    printf("about to setattr on %s\n", filename);

    ret = PVFS_sys_setattr(pinode_refn, resp_getattr.attr, &credentials, NULL);
    if (ret < 0)
    {
        fprintf(stderr, "setattr failed with errcode = %d\n", ret);
        return ret;
    }
    else
    {
        printf("setattr returned success\n");

        r_atime = (time_t)resp_getattr.attr.atime;
        r_mtime = (time_t)resp_getattr.attr.mtime;
        r_ctime = (time_t)resp_getattr.attr.ctime;

        printf("Set the following attributes\n");
        printf("Handle      : %llu\n", llu(pinode_refn.handle));
        printf("FSID        : %d\n", (int)pinode_refn.fs_id);
        printf("mask        : %d\n", resp_getattr.attr.mask);
        printf("uid         : %d\n", resp_getattr.attr.owner);
        printf("gid         : %d\n", resp_getattr.attr.group);
        printf("permissions : %d\n", resp_getattr.attr.perms);
        printf("atime       : %s", ctime(&r_atime));
        printf("mtime       : %s", ctime(&r_mtime));
        printf("ctime       : %s", ctime(&r_ctime));
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
 * vim: ts=8 sts=4 sw=4 expandtab
 */
