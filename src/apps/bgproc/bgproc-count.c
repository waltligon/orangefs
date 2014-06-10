/*
 * Copyright (C) 2014 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <sys/statvfs.h>
#include <sys/utsname.h>

#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>

#include <pvfs2.h>
#include <pvfs2-bgproc.h>

int readtree(PVFS_fs_id fsid, PVFS_credential *cred, char *path)
{
    PVFS_sysresp_lookup lookup;
    PVFS_sysresp_readdir readdir;
    PVFS_ds_position token;
    int ret;

    ret = PVFS_sys_lookup(fsid, path, cred, &lookup,
        PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_lookup", ret);
        return 1;
    }

    token = PVFS_ITERATE_START;
    while (token != PVFS_ITERATE_END)
    {
        uint32_t i;
        ret = PVFS_sys_readdir(lookup.ref, token, 32, cred, &readdir,
            0);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_readdir", ret);
            return 1;
        }
        for (i = 0; i < readdir.pvfs_dirent_outcount; i++)
        {
            puts(readdir.dirent_array[i].d_name);
        }
/*      free(readdir.dirent_array);*/
        token = readdir.token;
    }

    return 0;
}

int main(void)
{
    PVFS_credential cred;
    char path[PVFS_PATH_MAX];
    PVFS_fs_id fsid;
    int ret;

    if (bgproc_setup(1) != 0)
    {
        fprintf(stderr, "could not setup bgproc\n");
        return EXIT_FAILURE;
    }

    ret = PVFS_util_resolve(bgproc_fs, &fsid,
        path, PVFS_PATH_MAX);
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_resolve", ret);
        return EXIT_FAILURE;
    }
    if (*path == 0)
    {
        *path = '/';
        *(path+1) = 0;
    }

    ret = PVFS_util_gen_credential_defaults(&cred);
    if (ret < 0)
    {
        PVFS_perror("PVFS_sys_lookup", ret);
        return EXIT_FAILURE;
    }

    if (readtree(fsid, &cred, path) != 0)
    {
        fprintf(stderr, "could not read tree\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
