/* Client Service - file system routines */

#include <stdlib.h>
#include <stdio.h>

#include "pvfs2.h"

const PVFS_util_tab *tab;

/* TODO: global credentials for now */
const PVFS_credentials credentials;

/* initialize file systems */
int fs_initialize()
{
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

    /* generate credentials */
    PVFS_util_gen_credentials(&credentials);

    return 0;
}

struct PVFS_sys_mntent *fs_get_mntent(PVFS_fs_id fs_id)
{
    /* TODO: ignore fs_id right now,
       return first entry */
    return &tab->mntent_array[0];
}

int fs_resolve_path(const char *local_path, 
                    char *fs_path,
                    size_t fs_path_max)
{
    struct PVFS_sys_mntent *mntent;
    char *trans_path, *full_path;
    char *inptr, *outptr;
    PVFS_fs_id fs_id;
    int ret;

    if (local_path == NULL || fs_path == NULL ||
        fs_path_max == 0)
        return -1;

    trans_path = (char *) malloc(strlen(local_path) + 1);
    if (trans_path == NULL)
    {
        return -1;   /* TODO */
    }

    /* remove drive: if necessary */
    if (strlen(local_path) >= 2 && local_path[1] == ':')
        inptr = (char *) local_path + 2;
    else
        inptr = (char *) local_path;

    /* translate \'s to /'s */
    for (outptr = trans_path; *inptr; inptr++, outptr++)
    {
        if (*inptr == '\\')            
            *outptr = '/';
        else
            *outptr = *inptr;
    }
    *outptr = '\0';

    mntent = fs_get_mntent(0);
    
    full_path = (char *) malloc(strlen(trans_path) + 
                                strlen(mntent->mnt_dir) + 2);
    if (full_path == NULL)
    {
        free(trans_path);
        return -1;
    }
    
    /* prepend mount directory to path */
    strcpy(full_path, mntent->mnt_dir);
    if (full_path[strlen(full_path)-1] != '/')
        strcat(full_path, "/");
    strcat(full_path, trans_path);

    /* resolve the path against PVFS */
    ret = PVFS_util_resolve(full_path, &fs_id, fs_path, fs_path_max);

    free(full_path);
    free(trans_path);

    return ret;
}

/* lookup PVFS file path 
      returns 0 and handle if exists */
int fs_lookup(char *fs_path,
              PVFS_handle *handle)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_lookup resp;
    int ret;

    ret = PVFS_sys_lookup(mntent->fs_id, fs_path, &credentials, &resp,
                          TRUE, NULL);
    if (ret == 0)
        *handle = resp.ref.handle;

    return ret;
}

/* create file with specified path
      returns 0 and handle on success */
int fs_create(char *fs_path,
              PVFS_handle *handle)
{
    /* split path into path and file components */

    /* lookup parent path */

    /* create file */
}

int fs_finalize()
{
    /* TODO */
    PVFS_sys_finalize();

    return 0;
}
