/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include "client.h"
#include "pvfs2-util.h"

#define MAX_NUM_MNT  3

/*
  works with a pvfs2tab file that looks like:

tcp://localhost:3334/pvfs2-volume1 /mnt/pvfs pvfs2 encoding=le_bfield 0 0
tcp://localhost:3334/pvfs2-volume2 /mnt/pvfs2 pvfs2 encoding=le_bfield 0 0
tcp://localhost:3334/pvfs2-volume3 /mnt/pvfs3 pvfs2 encoding=le_bfield 0 0

  and of course with a running server that has these volumes initialized.
*/

int main(int argc, char **argv)
{
    int ret = -1;
    int i = 0;
    char buf[PVFS_NAME_MAX] = {0};
    PVFS_fs_id fs_id = PVFS_FS_ID_NULL;
    struct PVFS_sys_mntent mntent[MAX_NUM_MNT] =
    {
        { "tcp://localhost:3334", "pvfs2-volume1", 0, 0, 9,
          "/mnt/pvfs", "default" },
        { "tcp://localhost:3334", "pvfs2-volume2", 0, 0, 10,
          "/mnt/pvfs2", "default" },
        { "tcp://localhost:3334", "pvfs2-volume3", 0, 0, 11,
          "/mnt/pvfs3", "default" },
    };

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", ret);
	return (-1);
    }

    printf("*** All defaults initialized\n");

    /* make sure we can resolve all mnt points */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        ret = PVFS_util_resolve(mntent[i].mnt_dir, &fs_id,
                                buf, PVFS_NAME_MAX);
        if (ret)
        {
            printf("Failed to resolve mount point %s\n",
                   mntent[i].mnt_dir);
            PVFS_perror("Error", ret);
            return ret;
        }
        else
        {
            printf(" - Resolved PARSED mnt point %s to fs_id %d\n",
                   mntent[i].mnt_dir, (int)fs_id);
        }
    }

    /* remove the mount points */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        printf("Removing mount entry %d: %s\n",
               i, mntent[i].mnt_dir);
        ret = PVFS_sys_fs_remove(&mntent[i]);
        if (ret)
        {
            printf("Failed to remove mount entry %d\n",i);
            PVFS_perror("Error", ret);
            return ret;
        }
    }

    /* make sure we *can't* resolve all mnt points */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        ret = PVFS_util_resolve(mntent[i].mnt_dir, &fs_id,
                                buf, PVFS_NAME_MAX);
        if (ret == 0)
        {
            printf("Resolved an unresolvable mount point %s\n",
                   mntent[i].mnt_dir);
            return ret;
        }
        else
        {
            printf(" - Properly failed to resolve mnt point %s\n",
                   mntent[i].mnt_dir);
        }
    }

    /* re-add the mount points */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        printf("Adding dynamic mount entry %d: %s\n",
               i, mntent[i].mnt_dir);
        ret = PVFS_sys_fs_add(&mntent[i]);
        if (ret)
        {
            printf("Failed to add mount entry %d\n",i);
            PVFS_perror("Error", ret);
            return ret;
        }
    }

    /*
      make sure we can re-resolve all mnt points now that they've been
      moved to the dynamic area of the book keeping
    */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        ret = PVFS_util_resolve(mntent[i].mnt_dir, &fs_id,
                                buf, PVFS_NAME_MAX);
        if (ret)
        {
            printf("Failed to resolve mount point %s\n",
                   mntent[i].mnt_dir);
            PVFS_perror("Error", ret);
            return ret;
        }
        else
        {
            printf(" - Resolved DYNAMIC mnt point %s to fs_id %d\n",
                   mntent[i].mnt_dir, (int)fs_id);
        }
    }

    /* remove the dynamic mount points */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        printf("Removing dynamic mount entry %d: %s\n",
               i, mntent[i].mnt_dir);
        ret = PVFS_sys_fs_remove(&mntent[i]);
        if (ret)
        {
            printf("Failed to remove mount entry %d\n",i);
            PVFS_perror("Error", ret);
            return ret;
        }
    }

    /* make sure we *can't* resolve all mnt points */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        ret = PVFS_util_resolve(mntent[i].mnt_dir, &fs_id,
                                buf, PVFS_NAME_MAX);
        if (ret == 0)
        {
            printf("Resolved an unresolvable mount point %s\n",
                   mntent[i].mnt_dir);
            return ret;
        }
        else
        {
            printf(" - Properly failed to resolve mnt point %s\n",
                   mntent[i].mnt_dir);
        }
    }

    /* re-add the mount points */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        printf("Adding dynamic mount entry %d: %s\n",
               i, mntent[i].mnt_dir);
        ret = PVFS_sys_fs_add(&mntent[i]);
        if (ret)
        {
            printf("Failed to add mount entry %d\n",i);
            PVFS_perror("Error", ret);
            return ret;
        }
    }

    /* re-resolve one more time -- to be sure ;-) */
    for(i = 0; i < MAX_NUM_MNT; i++)
    {
        ret = PVFS_util_resolve(mntent[i].mnt_dir, &fs_id,
                                buf, PVFS_NAME_MAX);
        if (ret)
        {
            printf("Failed to resolve mount point %s\n",
                   mntent[i].mnt_dir);
            PVFS_perror("Error", ret);
            return ret;
        }
        else
        {
            printf(" - Resolved DYNAMIC mnt point %s to fs_id %d\n",
                   mntent[i].mnt_dir, (int)fs_id);
        }
    }

    ret = PVFS_sys_finalize();
    if (ret < 0)
    {
	printf("finalizing sysint failed with errcode = %d\n", ret);
	return (-1);
    }
    return (0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
