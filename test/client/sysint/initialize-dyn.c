/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <assert.h>
#include "client.h"
#include "pvfs2-util.h"

#define MAX_NUM_MNT  3

static int copy_mntent(
    struct PVFS_sys_mntent *dest_mntent,
    struct PVFS_sys_mntent *src_mntent)
{
    int ret = -PVFS_ENOMEM;

    if (dest_mntent && src_mntent)
    {
        memset(dest_mntent, 0, sizeof(struct PVFS_sys_mntent));

        dest_mntent->pvfs_config_server =
            strdup(src_mntent->pvfs_config_server);
        assert(dest_mntent->pvfs_config_server);

        dest_mntent->pvfs_fs_name = strdup(src_mntent->pvfs_fs_name);
        assert(dest_mntent->pvfs_fs_name);

        if (src_mntent->mnt_dir)
        {
            dest_mntent->mnt_dir = strdup(src_mntent->mnt_dir);
            assert(dest_mntent->mnt_dir);
        }
        if (src_mntent->mnt_opts)
        {
            dest_mntent->mnt_opts = strdup(src_mntent->mnt_opts);
            assert(dest_mntent->mnt_opts);
        }
        dest_mntent->flowproto = src_mntent->flowproto;
        dest_mntent->encoding = src_mntent->encoding;
        dest_mntent->fs_id = src_mntent->fs_id;

        /* TODO: memory allocation error handling */
        ret = 0;
    }
    return ret;
}

int main(int argc, char **argv)
{
    int ret = -1;
    int i = 0;
    char buf[PVFS_NAME_MAX] = {0};
    PVFS_fs_id fs_id = PVFS_FS_ID_NULL;
    struct PVFS_sys_mntent *mntent = NULL;

    const PVFS_util_tab* tab = PVFS_util_parse_pvfstab(NULL);
    if (!tab)
    {
        gossip_err(
            "Error: failed to find any pvfs2 file systems in the "
            "standard system tab files.\n");
        return(-PVFS_ENOENT);
    }

    /* initialize pvfs system interface */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        return(ret);
    }

    /* add in any file systems we found in the fstab */
    for(i = 0; i < tab->mntent_count; i++)
    {
        ret = PVFS_sys_fs_add(&tab->mntent_array[i]);
        if (ret != 0)
        {
            PVFS_perror("Invalid fs information in pvfstab file", ret);
            return ret;
        }
    }

    /* copy all mount ents for our own usage */
    mntent = (struct PVFS_sys_mntent *)malloc(
        tab->mntent_count * sizeof(struct PVFS_sys_mntent));
    assert(mntent);

    for(i = 0; i < tab->mntent_count; i++)
    {
        ret = copy_mntent(&mntent[i], &tab->mntent_array[i]);
        assert(ret == 0);
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
 * vim: ts=8 sts=4 sw=4 expandtab
 */
