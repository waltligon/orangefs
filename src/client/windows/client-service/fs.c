/* Client Service - file system routines */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "pvfs2.h"
#include "str-utils.h"

const PVFS_util_tab *tab;

/* split path into base dir and entry name components */
int split_path(char *fs_path, 
               char *base_dir,
               int base_dir_len,
               char *entry_name,
               int entry_name_len)
{
    int ret;

    /* get base dir */
    ret = PINT_get_base_dir(fs_path, base_dir, base_dir_len);

    if (ret != 0)
        return ret;

    /* get entry name */
    ret = PINT_remove_base_dir(fs_path, entry_name, entry_name_len);

    return ret;
}

/* initialize file systems */
int fs_initialize(const char *tabfile, 
                  char *error_msg,
                  size_t error_msg_len)
{
    int ret, i, found_one = 0;
    char errbuf[256];

    /* read tab file */
    tab = PVFS_util_parse_pvfstab(tabfile);
    if (!tab)
    {
        _snprintf(error_msg, error_msg_len, "fs_initialize: failed to parse %s", 
            tabfile);
        return -1;
    }

    /* initialize PVFS */
    /* TODO: debug settings */
    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        PVFS_strerror_r(ret, errbuf, 256);
        _snprintf(error_msg, error_msg_len, "PVFS_sys_initialize: %s", errbuf);
        return ret;
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
        _snprintf(error_msg, error_msg_len, "fs_initialize: could not initialize any "
            "file systems from %s", tab->tabfile_name);
     
        PVFS_sys_finalize();
        return -1;
    }

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
    /* append path */
    if (full_path[strlen(full_path)-1] != '/')
        strcat(full_path, "/");
    if (trans_path[0] == '/')
        strcat(full_path, trans_path+1);
    else
        strcat(full_path, trans_path);
    strncpy(fs_path, full_path, fs_path_max);

    /* resolve the path against PVFS */
    ret = PVFS_util_resolve(full_path, &fs_id, fs_path, fs_path_max);

    free(full_path);
    free(trans_path);

    return ret;
}

/* lookup PVFS file path 
      returns 0 and handle if exists */
int fs_lookup(char *fs_path,
              PVFS_credentials *credentials,
              PVFS_handle *handle)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    char *real_path;
    PVFS_sysresp_lookup resp;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_fs_id fs_id;
    int ret, link_flag;

    real_path = strdup(fs_path);
    do 
    {
        link_flag = FALSE;

        /* lookup the given path on the FS */
        ret = PVFS_sys_lookup(mntent->fs_id, real_path, credentials, &resp,
            FALSE, NULL);
        if (ret != 0)
            break;

        /* check if it's a link */
        memset(&resp_getattr, 0, sizeof(resp_getattr));
        ret = PVFS_sys_getattr(resp.ref, PVFS_ATTR_SYS_LNK_TARGET, credentials,
            &resp_getattr, NULL);
        if (ret != 0)
            break;

        if (resp_getattr.attr.link_target != NULL)
        {
            link_flag = TRUE;
            /* get file name */
            free(real_path);
            real_path = (char *) malloc(PVFS_NAME_MAX);
            ret = PVFS_util_resolve(resp_getattr.attr.link_target, &fs_id, 
                real_path, PVFS_NAME_MAX);
            /* free attr buffer */
            free(resp_getattr.attr.link_target);
        }
    } while (link_flag);

    free(real_path);

    if (ret == 0)
        *handle = resp.ref.handle;

    return ret;
}

/* create file with specified path
      returns 0 and handle on success */
int fs_create(char *fs_path,
              PVFS_credentials *credentials,
              PVFS_handle *handle)
{    
    char *base_dir, *entry_name;
    int len;
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    int ret;
    PVFS_sys_attr attr;
    PVFS_sysresp_create resp_create;

    if (fs_path == NULL || strlen(fs_path) == 0)
        return -1;
    
    /* cannot be only a dir */
    if (fs_path[strlen(fs_path)-1] == '/')
        return -1;

    /* split path into path and file components */
    len = strlen(fs_path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    ret = split_path(fs_path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto fs_create_exit;

    /* lookup parent path */
    ret = PVFS_sys_lookup(mntent->fs_id, base_dir, credentials, &resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_create_exit;

    parent_ref.fs_id = resp_lookup.ref.fs_id;
    parent_ref.handle = resp_lookup.ref.handle;

    /* create file */
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials->uid;
    attr.group = credentials->gid;
    /* default permissions: rwxr-xr-x */
    attr.perms = 0755;
    attr.atime = attr.mtime = attr.ctime = time(NULL);

    ret = PVFS_sys_create(entry_name, parent_ref, attr,
              credentials, NULL, &resp_create, NULL, NULL);
    if (ret)
        goto fs_create_exit;

    *handle = resp_create.ref.handle;

fs_create_exit:
    free(entry_name);
    free(base_dir);

    return ret;
}

/* remove specified directory or file */
int fs_remove(char *fs_path,
              PVFS_credentials *credentials)
{
    char *base_dir, *entry_name;
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    int len, ret;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;

    if (fs_path == NULL || strlen(fs_path) == 0)
        return -1;

    /* split path into path and file components */
    len = strlen(fs_path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    ret = split_path(fs_path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto fs_remove_exit;

    /* lookup parent entry */
    ret = PVFS_sys_lookup(mntent->fs_id, base_dir, credentials, &resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_remove_exit;

    parent_ref.fs_id = resp_lookup.ref.fs_id;
    parent_ref.handle = resp_lookup.ref.handle;

    ret = PVFS_sys_remove(entry_name, parent_ref, credentials, NULL);

fs_remove_exit:
    free(entry_name);
    free(base_dir);

    return ret;
}

int fs_rename(char *old_path, 
              char *new_path,
              PVFS_credentials *credentials)
{
    char *old_base_dir, *old_entry_name,
         *new_base_dir, *new_entry_name;
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    int len, ret;
    PVFS_sysresp_lookup old_resp_lookup, new_resp_lookup;
    PVFS_object_ref old_parent_ref, new_parent_ref;

    if (old_path == NULL || strlen(old_path) == 0 ||
        new_path == NULL || strlen(new_path) == 0)
        return -1;

    /* split paths into path and file components */
    len = strlen(old_path) + 1;
    old_base_dir = (char *) malloc(len);    
    old_entry_name = (char *) malloc(len);
    ret = split_path(old_path, old_base_dir, len, old_entry_name, len);
    if (ret != 0)
        goto fs_rename_exit;

    /* lookup parent entry */
    ret = PVFS_sys_lookup(mntent->fs_id, old_base_dir, credentials, &old_resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_rename_exit;

    old_parent_ref.fs_id = old_resp_lookup.ref.fs_id;
    old_parent_ref.handle = old_resp_lookup.ref.handle;

    len = strlen(new_path) + 1;
    new_base_dir = (char *) malloc(len);    
    new_entry_name = (char *) malloc(len);
    ret = split_path(new_path, new_base_dir, len, new_entry_name, len);
    if (ret != 0)
        goto fs_rename_exit;

    /* lookup parent entry */
    ret = PVFS_sys_lookup(mntent->fs_id, new_base_dir, credentials, &new_resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_rename_exit;

    new_parent_ref.fs_id = new_resp_lookup.ref.fs_id;
    new_parent_ref.handle = new_resp_lookup.ref.handle;

    /* rename/move the file */
    ret = PVFS_sys_rename(old_entry_name, old_parent_ref, new_entry_name,
                          new_parent_ref, credentials, NULL);

fs_rename_exit:
    
    free(new_entry_name);
    free(new_base_dir);
    free(old_entry_name);
    free(old_base_dir);

    return ret;
}

int fs_truncate(char *fs_path,
                PVFS_size size,
                PVFS_credentials *credentials)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    int ret;
    PVFS_sysresp_lookup resp_lookup;

    /* lookup file */
    ret = PVFS_sys_lookup(mntent->fs_id, fs_path, credentials, &resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_truncate_exit;

    ret = PVFS_sys_truncate(resp_lookup.ref, size, credentials, NULL);

fs_truncate_exit:

    return ret;
}

int fs_getattr(char *fs_path,
               PVFS_credentials *credentials,
               PVFS_sys_attr *attr)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    char *real_path;
    int ret, link_flag;
    PVFS_fs_id fs_id;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_getattr resp_getattr;

    if (fs_path == NULL || strlen(fs_path) == 0 ||
        attr == NULL)
        return -PVFS_EINVAL;

    /* lookup file */
    real_path = strdup(fs_path);
    do {
        link_flag = FALSE;

        ret = PVFS_sys_lookup(mntent->fs_id, real_path, credentials, &resp_lookup,
            FALSE, NULL);
        if (ret != 0)
            break;
        
        /* read all attributes */
        memset(&resp_getattr, 0, sizeof(resp_getattr));
        ret = PVFS_sys_getattr(resp_lookup.ref, PVFS_ATTR_SYS_ALL_NOHINT, 
                           credentials, &resp_getattr, NULL);
        if (ret != 0)
            break;

        /* get attributes for link target */
        if (resp_getattr.attr.link_target != NULL)
        {
            link_flag = TRUE;
            /* get file name */
            free(real_path);
            real_path = (char *) malloc(PVFS_NAME_MAX);
            ret = PVFS_util_resolve(resp_getattr.attr.link_target, &fs_id, 
                real_path, PVFS_NAME_MAX);
            /* free attr buffers */
            free(resp_getattr.attr.link_target);
            if (resp_getattr.attr.dist_name != NULL)
                free(resp_getattr.attr.dist_name);
            if (resp_getattr.attr.dist_params != NULL)
                free(resp_getattr.attr.dist_params);            
        }
    } while (link_flag);

    free(real_path);

    if (ret != 0)
        goto fs_getattr_exit;

    memcpy(attr, &resp_getattr.attr, sizeof(PVFS_sys_attr));

fs_getattr_exit:

    return ret;
}

int fs_setattr(char *fs_path,
               PVFS_sys_attr *attr,
               PVFS_credentials *credentials)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    int ret;
    PVFS_sysresp_lookup resp_lookup;

    if (fs_path == NULL || strlen(fs_path) == 0 ||
        attr == NULL)
        return -PVFS_EINVAL;

    ret = PVFS_sys_lookup(mntent->fs_id, fs_path, credentials, &resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_setattr_exit;

    /* set attributes */
    ret = PVFS_sys_setattr(resp_lookup.ref, *attr, credentials, NULL);

fs_setattr_exit:

    return ret;
}

int fs_mkdir(char *fs_path,
             PVFS_credentials *credentials,
             PVFS_handle *handle)
{
    char *base_dir, *entry_name;
    int len;
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    PVFS_sys_attr attr;
    PVFS_sysresp_mkdir resp_mkdir;
    int ret;

    if (fs_path == NULL || strlen(fs_path) == 0)
        return -1;
    
    /* split path into path and file components */
    len = strlen(fs_path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    ret = split_path(fs_path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto fs_mkdir_exit;

    /* lookup parent path */
    ret = PVFS_sys_lookup(mntent->fs_id, base_dir, credentials, &resp_lookup,
                          TRUE, NULL);

    if (ret != 0)
        goto fs_mkdir_exit;

    parent_ref.fs_id = resp_lookup.ref.fs_id;
    parent_ref.handle = resp_lookup.ref.handle;

    /* create file */
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials->uid;
    attr.group = credentials->gid;
    /* default permissions: rwxr-xr-x */
    attr.perms = 0755;
    attr.atime = attr.mtime = attr.ctime = time(NULL);

    ret = PVFS_sys_mkdir(entry_name, parent_ref, attr, credentials,
                         &resp_mkdir, NULL);

    if (ret == 0)
        *handle = resp_mkdir.ref.handle;

fs_mkdir_exit:
    free(entry_name);
    free(base_dir);

    return ret;
}

int fs_io(enum PVFS_io_type io_type,
          char *fs_path,
          void *buffer,
          size_t buffer_len,
          uint64_t offset,
          PVFS_size *op_len,
          PVFS_credentials *credentials)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref object_ref;
    PVFS_Request file_req, mem_req;
    PVFS_sysresp_io resp_io;
    int ret;

    if (fs_path == NULL || strlen(fs_path) == 0 ||
        buffer == NULL)
        return -PVFS_EINVAL;

    /* lookup file */
    ret = PVFS_sys_lookup(mntent->fs_id, fs_path, credentials, &resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_io_exit;

    /* copy object ref */
    object_ref.fs_id = resp_lookup.ref.fs_id;
    object_ref.handle = resp_lookup.ref.handle;

    /* get memory buffer */
    file_req = PVFS_BYTE;

    ret = PVFS_Request_contiguous(buffer_len, PVFS_BYTE, &(mem_req));
    if (ret != 0)
        goto fs_io_exit;

    /* perform io operation */
    ret = PVFS_sys_io(object_ref, file_req, offset, buffer, mem_req,
                      credentials, &resp_io, io_type, NULL);
    if (ret == 0 && op_len != NULL)
    {
        *op_len = resp_io.total_completed;
    }

    PVFS_Request_free(&mem_req);

fs_io_exit:

    return ret;
}

int fs_flush(char *fs_path,
             PVFS_credentials *credentials)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    int ret;
    PVFS_sysresp_lookup resp_lookup;

    if (fs_path == NULL || strlen(fs_path) == 0)
        return -PVFS_EINVAL;

    /* lookup file */
    ret = PVFS_sys_lookup(mntent->fs_id, fs_path, credentials, &resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_flush_exit;

    /* flush file */
    ret = PVFS_sys_flush(resp_lookup.ref, credentials, NULL);

fs_flush_exit:

    return ret;
}

int fs_find_next_file(char *fs_path, 
                      PVFS_ds_position *token,
                      PVFS_credentials *credentials,
                      char *filename,
                      size_t max_name_len)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    int ret;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_readdir resp_readdir;

    if (fs_path == NULL || strlen(fs_path) == 0 ||
        token == NULL || filename == NULL || max_name_len == 0)
        return -PVFS_EINVAL;

    /* lookup file */
    ret = PVFS_sys_lookup(mntent->fs_id, fs_path, credentials, &resp_lookup,
                          TRUE, NULL);
    if (ret != 0)
        goto fs_readdir_exit;

    /* read one entry, starting with token */
    ret = PVFS_sys_readdir(resp_lookup.ref, *token, 1, credentials, 
                           &resp_readdir, NULL);
    if (ret != 0)
        goto fs_readdir_exit;

    /* copy output results */
    if (resp_readdir.pvfs_dirent_outcount != 0)
    {
        *token = resp_readdir.token;
        
        strncpy(filename, resp_readdir.dirent_array[0].d_name, max_name_len);
    }
    else
    {
        /* return empty string when done */
        filename[0] = '\0';
    }

fs_readdir_exit:

    return ret;
}

int fs_find_first_file(char *fs_path,
                       PVFS_ds_position *token,
                       PVFS_credentials *credentials,
                       char *filename,
                       size_t max_name_len)
{
    if (token == NULL)
    {
        return -PVFS_EINVAL;
    }

   *token = PVFS_READDIR_START;
   return fs_find_next_file(fs_path, token, credentials, filename, max_name_len);
}

int fs_get_diskfreespace(PVFS_credentials *credentials,
                         PVFS_size *free_bytes, 
                         PVFS_size *total_bytes)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_statfs resp_statfs;
    int ret;

    if (free_bytes == NULL || total_bytes == NULL)
    {
        return -PVFS_EINVAL;
    }

    ret = PVFS_sys_statfs(mntent->fs_id, credentials, &resp_statfs, NULL);

    if (ret == 0)
    {
        *free_bytes = resp_statfs.statfs_buf.bytes_available;
        *total_bytes = resp_statfs.statfs_buf.bytes_total;
    }

    return ret;
}

PVFS_fs_id fs_get_id(int fs_num)
{
    /* TODO: ignore fs_num for now */
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);

    return mntent ? mntent->fs_id : 0;
}

char *fs_get_name(int fs_num)
{
    /* TODO: ignore fs_num for now */
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);

    return mntent ? mntent->pvfs_fs_name : "PVFS2";
}

int fs_finalize()
{
    /* TODO */
    PVFS_sys_finalize();

    return 0;
}
