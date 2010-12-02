/* Client Service - file system routines */

#include <stdlib.h>
#include <stdio.h>

#include "pvfs2.h"

/* initialize file systems */
int fs_initialize()
{
    const PVFS_util_tab *tab;
    int ret, i, found_one = 0;

    /* read tab file */
    tab = PVFS_util_parse_pvfstab(NULL);
    if (!tab)
    {
        fprintf(stderr, "Error: failed to parse pvfstab\n");
        return -1;
    }

    /* initialize PVFS */
    /* TODO: debug settings */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_initialize", ret);
        return -1;
    }
    
    /* initialize file systems */
    for (i = 0; i < tab->mntent_count; i++)
    {
        ret = PVFS_sys_fs_add(&tab->mntent_array[i]);
        if (ret == 0)
            found_one = 1;
    }

    if (!found_one)
    {
        fprintf(stderr, "Error: could not initialize any file systems "
            "from %s\n", tab->tabfile_name);
     
        PVFS_sys_finalize();
        return -1;
    }

    return 0;
}

int fs_finalize()
{
    /* TODO */
    PVFS_sys_finalize();

    return 0;
}