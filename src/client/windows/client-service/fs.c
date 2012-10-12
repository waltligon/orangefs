/*
 * (C) 2010-2011 Clemson University and Omnibond LLC
 *
 * See COPYING in top-level directory.
 */

/* 
 * Client Service - file system routines 
 * These routines provide a layer of abstraction between the Dokan
 * callbacks (dokan-interface.c) and OrangeFS. 
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "pvfs2.h"
#include "str-utils.h"

#include "client-service.h"

const PVFS_util_tab *tab;

int sys_get_symlink_attr(char *fs_path,
                         char *target,
                         PVFS_object_ref *ref, 
                         PVFS_credentials *credentials,
                         PVFS_sys_attr *attr);

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

        PINT_release_pvfstab();
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
    /* struct PVFS_sys_mntent *mntent; */
    /* char *trans_path, *full_path; */
    char *inptr, *outptr;
    /* PVFS_fs_id fs_id; */
    size_t count;

    if (local_path == NULL || fs_path == NULL ||
        fs_path_max == 0)
        return -1;

    /*
    trans_path = (char *) malloc(strlen(local_path) + 1);
    if (trans_path == NULL)
    {
        return -1;   /* TODO */
    /* } */

    /* remove drive: if necessary */
    if (strlen(local_path) >= 2 && local_path[1] == ':')
        inptr = (char *) local_path + 2;
    else
        inptr = (char *) local_path;

    /* translate \'s to /'s */
    for (outptr = fs_path, count = 0; 
         *inptr && (count < fs_path_max);
         inptr++, outptr++, count++)
    {
        if (*inptr == '\\')            
            *outptr = '/';
        else
            *outptr = *inptr;
    }
    *outptr = '\0';

#if 0
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
    /* strncpy(fs_path, full_path, fs_path_max); */

    /* resolve the path against PVFS */
    ret = PVFS_util_resolve(full_path, &fs_id, fs_path, fs_path_max);

    free(full_path);
    free(trans_path);
#endif 

    return 0;
}

#define FS_MAX_LINKS    256

/* Workaround to follow PVFS2 file links */
int sys_lookup_follow_links(PVFS_fs_id fs_id,
                            char *fs_path,
                            PVFS_credentials *credentials,
                            PVFS_sysresp_lookup *resp,
                            PVFS_sys_attr *attr)
{
    char *real_path, *link_path;
    PVFS_sysresp_getattr resp_getattr;
    int ret, link_flag, count;

    if (fs_path == NULL || strlen(fs_path) == 0 || credentials == NULL ||
        resp == NULL)
        return -1;

    /* copy to be used for link paths */
    real_path = strdup(fs_path);
    count = 0;
    do 
    {
        link_flag = FALSE;

        /* lookup the given path on the FS */
        ret = PVFS_sys_lookup(fs_id, real_path, credentials, resp,
            PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
        if (ret != 0)
            break;

        /* check if it's a link */
        memset(&resp_getattr, 0, sizeof(resp_getattr));
        ret = PVFS_sys_getattr(resp->ref, PVFS_ATTR_SYS_ALL_NOHINT, credentials,
            &resp_getattr, NULL);
        if (ret != 0)
            break;

        if (resp_getattr.attr.link_target != NULL)
        {
            /* we have a link -- compute the target */
            link_flag = TRUE;
            link_path = (char *) malloc(PVFS_PATH_MAX + 4);
            if (link_path == NULL)
            {
                PVFS_util_release_sys_attr(&resp_getattr.attr);
                ret = -PVFS_ENOMEM;
                break;
            }
            /* if link target is not absolute, prepend path */
            memset(link_path, 0, PVFS_PATH_MAX + 4);
            if (resp_getattr.attr.link_target[0] != '/')
            {
                char *lastslash = strrchr(real_path, '/');
                if (lastslash)
                {
                    /* copy path including slash */
                    strncpy(link_path, real_path, lastslash - real_path + 1);
                    strncat(link_path, resp_getattr.attr.link_target, 
                        PVFS_PATH_MAX - strlen(link_path));
                }
                else
                {
                    free(link_path);
                    PVFS_util_release_sys_attr(&resp_getattr.attr);
                    ret = -PVFS_EINVAL;                    
                    break;
                }
            }
            else
            {
                /* use absolute link target */
                strncpy(link_path, resp_getattr.attr.link_target, PVFS_PATH_MAX);
            }
            link_path[PVFS_PATH_MAX-1] = '\0';
            /* get file name */
            free(real_path);
            real_path = (char *) malloc(PVFS_PATH_MAX + 4);
            ret = PVFS_util_resolve(link_path, &fs_id, real_path, PVFS_PATH_MAX);
            if (ret == -PVFS_ENOENT)
            {
                /* link may not include the mount point, e.g. /mnt/pvfs2.
                   in this case just use link_path */
                strncpy(real_path, link_path, PVFS_PATH_MAX);
                ret = 0;
            }
            free(link_path);
            /* free attr buffer */            
            PVFS_util_release_sys_attr(&resp_getattr.attr);
        }
        else
        {
            /* copy the attr to the buffer */
            if (attr != NULL)
            {
                memcpy(attr, &resp_getattr.attr, sizeof(PVFS_sys_attr));
            }
            else
            {
                PVFS_util_release_sys_attr(&resp_getattr.attr);
            }
        }                
    } while (ret == 0 && link_flag && (++count) < FS_MAX_LINKS);

    free(real_path);

    if (ret == 0 && count >= FS_MAX_LINKS)
    {
        ret = -PVFS_EMLINK;
    }

    return ret;
}

/* get the attributes of the target of a symlink */
int sys_get_symlink_attr(char *fs_path,
                         char *target,
                         PVFS_object_ref *ref, 
                         PVFS_credentials *credentials,
                         PVFS_sys_attr *attr)
{
    int ret;
    char link_path[PVFS_PATH_MAX + 4];
    PVFS_sysresp_lookup resp_lookup;

    if (fs_path == NULL || target == NULL || ref == NULL ||
        credentials == NULL || attr == NULL)
    {
        return -PVFS_EINVAL;
    }

    if (strlen(fs_path) == 0 || strlen(target) == 0)
    {
        return -PVFS_EINVAL;
    }

    /* if target is relative, append to fs_path */
    memset(link_path, 0, PVFS_PATH_MAX+4);
    if (target[0] != '/') 
    {
        strncpy(link_path, fs_path, PVFS_PATH_MAX);        
        if (link_path[strlen(link_path)-1] != '/')
        {
            strcat(link_path, "/");
        }
        strncat(link_path, target, PVFS_PATH_MAX - strlen(link_path));
    }
    else
    {
        PVFS_fs_id fs_id;
        /* use PVFS_util_resolve to remove preceding mount point */
        ret = PVFS_util_resolve(target, &fs_id, link_path, PVFS_PATH_MAX);
        if (ret == -PVFS_ENOENT)
        {
            /* just use target as path */
            strncpy(link_path, target, PVFS_PATH_MAX);
        }
        else if (ret != 0)
        {
            return ret;
        }
    }

    /* lookup the target and retrieve the attributes */
    ret = sys_lookup_follow_links(ref->fs_id, link_path, credentials, &resp_lookup, 
        attr);

    return ret;
}

/* lookup PVFS file path 
   returns 0 and handle if exists */
int fs_lookup(char *fs_path,
              PVFS_credentials *credentials,
              PVFS_handle *handle)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_lookup resp;
    int ret;

    if (fs_path == NULL || credentials == NULL || handle == NULL)
        return -1;

    /* lookup the given path on the FS - do not follow links */
    ret = PVFS_sys_lookup(mntent->fs_id, fs_path, credentials, &resp,
        PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);

    if (ret == 0)
        *handle = resp.ref.handle;

    return ret;
}

/* create file with specified path
      returns 0 and handle on success */
int fs_create(char *fs_path,
              PVFS_credentials *credentials,
              PVFS_handle *handle,
              unsigned int perms)
{    
    char *base_dir, *entry_name;
    size_t len;
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    int ret;
    PVFS_sys_attr attr;
    PVFS_sysresp_create resp_create;

    if (fs_path == NULL || strlen(fs_path) == 0 || credentials == NULL)
        return -PVFS_EINVAL;
    
    /* cannot be only a dir */
    if (fs_path[strlen(fs_path)-1] == '/')
        return -PVFS_EINVAL;

    /* split path into path and file components */
    len = strlen(fs_path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    if (base_dir == NULL || entry_name == NULL)
    {
        return -PVFS_ENOMEM;
    }
    ret = split_path(fs_path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto fs_create_exit;

    /* lookup parent path - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, base_dir, credentials, 
        &resp_lookup, NULL);
    if (ret != 0)
        goto fs_create_exit;

    parent_ref.fs_id = resp_lookup.ref.fs_id;
    parent_ref.handle = resp_lookup.ref.handle;

    /* create file */
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials->uid;
    attr.group = credentials->gid;
    /* configurable in options */
    attr.perms = perms;
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
    size_t len;
    int ret;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;

    if (fs_path == NULL || strlen(fs_path) == 0 || credentials == NULL)
        return -PVFS_EINVAL;

    /* split path into path and file components */
    len = strlen(fs_path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    if (base_dir == NULL || entry_name == NULL)
    {
        return -PVFS_ENOMEM;
    }
    ret = split_path(fs_path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto fs_remove_exit;

    /* lookup parent entry - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, base_dir, credentials, 
        &resp_lookup, NULL);
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
    size_t len;
    int ret;
    PVFS_sysresp_lookup old_resp_lookup, new_resp_lookup;
    PVFS_object_ref old_parent_ref, new_parent_ref;

    if (old_path == NULL || strlen(old_path) == 0 ||
        new_path == NULL || strlen(new_path) == 0 ||
        credentials == NULL)
        return -PVFS_EINVAL;

    /* split paths into path and file components */
    len = strlen(old_path) + 1;
    old_base_dir = (char *) malloc(len);    
    old_entry_name = (char *) malloc(len);
    if (old_base_dir == NULL || old_entry_name == NULL)
    {
        return -PVFS_ENOMEM;
    }
    ret = split_path(old_path, old_base_dir, len, old_entry_name, len);
    if (ret != 0)
        goto fs_rename_exit;

    /* lookup parent entry - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, old_base_dir, credentials, 
        &old_resp_lookup, NULL);
    if (ret != 0)
        goto fs_rename_exit;

    old_parent_ref.fs_id = old_resp_lookup.ref.fs_id;
    old_parent_ref.handle = old_resp_lookup.ref.handle;

    len = strlen(new_path) + 1;
    new_base_dir = (char *) malloc(len);    
    new_entry_name = (char *) malloc(len);
    if (new_base_dir == NULL || new_entry_name == NULL)
    {
        return -PVFS_ENOMEM;
    }
    ret = split_path(new_path, new_base_dir, len, new_entry_name, len);
    if (ret != 0)
        goto fs_rename_exit;

    /* lookup parent entry - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, new_base_dir, credentials, 
        &new_resp_lookup, NULL);
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

    if (fs_path == NULL || strlen(fs_path) == 0 || credentials == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* lookup file - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, fs_path, credentials, 
        &resp_lookup, NULL);
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
    int ret;
    PVFS_sysresp_lookup resp_lookup;

    if (fs_path == NULL || strlen(fs_path) == 0 ||
        attr == NULL || credentials == NULL)
        return -PVFS_EINVAL;

    /* lookup file - follow links 
       attributes will be read and placed in attr */
    ret = sys_lookup_follow_links(mntent->fs_id, fs_path, credentials, 
        &resp_lookup, attr);
    if (ret != 0)
        goto fs_getattr_exit;

    /* free attr bufs */
    PVFS_util_release_sys_attr(attr);
   
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
        attr == NULL || credentials == NULL)
        return -PVFS_EINVAL;

    /* lookup file - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, fs_path, credentials, 
        &resp_lookup, NULL);
    if (ret != 0)
        goto fs_setattr_exit;

    /* set attributes */
    ret = PVFS_sys_setattr(resp_lookup.ref, *attr, credentials, NULL);

fs_setattr_exit:

    return ret;
}

int fs_mkdir(char *fs_path,
             PVFS_credentials *credentials,
             PVFS_handle *handle,
             unsigned int perms)
{
    char *base_dir, *entry_name;
    size_t len;
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    PVFS_sys_attr attr;
    PVFS_sysresp_mkdir resp_mkdir;
    int ret;

    if (fs_path == NULL || strlen(fs_path) == 0 || credentials == NULL)
        return -PVFS_EINVAL;
    
    /* split path into path and file components */
    len = strlen(fs_path) + 1;
    base_dir = (char *) malloc(len);    
    entry_name = (char *) malloc(len);
    if (base_dir == NULL || entry_name == NULL)
    {
        return -PVFS_ENOMEM;
    }
    ret = split_path(fs_path, base_dir, len, entry_name, len);
    if (ret != 0)
        goto fs_mkdir_exit;

    /* lookup parent path - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, base_dir, credentials, 
        &resp_lookup, NULL);

    if (ret != 0)
        goto fs_mkdir_exit;

    parent_ref.fs_id = resp_lookup.ref.fs_id;
    parent_ref.handle = resp_lookup.ref.handle;

    /* create file */
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = credentials->uid;
    attr.group = credentials->gid;
    /* configurable in options */
    attr.perms = perms;
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
        buffer == NULL || credentials == NULL)
        return -PVFS_EINVAL;

    /* lookup file - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, fs_path, credentials, 
        &resp_lookup, NULL);
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

    if (fs_path == NULL || strlen(fs_path) == 0 || credentials == NULL)
        return -PVFS_EINVAL;

    /* lookup file - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, fs_path, credentials, 
        &resp_lookup, NULL);
    if (ret != 0)
        goto fs_flush_exit;

    /* flush file */
    ret = PVFS_sys_flush(resp_lookup.ref, credentials, NULL);

fs_flush_exit:

    return ret;
}

int fs_find_files(char *fs_path, 
                  PVFS_credentials *credentials,              
                  PVFS_ds_position *token,
                  int32_t incount,
                  int32_t *outcount,
                  char **filename_array,
                  PVFS_sys_attr *attr_array)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    int ret, i;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_readdirplus resp_readdirplus;
    PVFS_object_ref ref;
    PVFS_sysresp_getattr resp_getattr;

    if (fs_path == NULL || strlen(fs_path) == 0 || token == NULL ||
        outcount == NULL || filename_array == NULL || attr_array == NULL ||
        credentials == NULL)
        return -PVFS_EINVAL;

    /* lookup path - follow links */
    ret = sys_lookup_follow_links(mntent->fs_id, fs_path, credentials, 
        &resp_lookup, NULL);
    if (ret != 0)
        goto fs_readdir_exit;

    /* read up to incount entries, starting with token */
    memset(&resp_readdirplus, 0, sizeof(resp_readdirplus));
    ret = PVFS_sys_readdirplus(resp_lookup.ref, *token, incount, credentials, 
                               PVFS_ATTR_SYS_ALL_NOHINT, &resp_readdirplus, NULL);
    if (ret != 0)
        goto fs_readdir_exit;

    /* copy output results */
    *outcount = resp_readdirplus.pvfs_dirent_outcount;
    if (*outcount != 0)
    {
        *token = resp_readdirplus.token;
            
        for (i = 0; i < *outcount; i++)
        {
            strncpy(filename_array[i], resp_readdirplus.dirent_array[i].d_name, PVFS_NAME_MAX);
            if (resp_readdirplus.stat_err_array[i] == 0)
            {
                memcpy(&attr_array[i], &resp_readdirplus.attr_array[i], sizeof(PVFS_sys_attr));
                /* TODO: DEBUG */
                if (resp_readdirplus.attr_array[i].link_target)
                {
                    DbgPrint("    %s link: %s\n", filename_array[i], resp_readdirplus.attr_array[i].link_target);                    
                }
            }
            else
            {
                /* attempt to get attrs with PVFS_sys_getattr */
                ref.fs_id = mntent->fs_id;
                ref.handle = resp_readdirplus.dirent_array[i].handle;
                memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
                ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL_NOHINT, credentials,
                    &resp_getattr, NULL);
                if (ret == 0)
                {
                    memcpy(&attr_array[i], &resp_getattr.attr, sizeof(PVFS_sys_attr));                    
                }
                else {
                    break;
                }
            }
            /* get attributes of symbolic link if applicable */
            if (attr_array[i].link_target != NULL)
            {
                ref.fs_id = mntent->fs_id;
                ref.handle = resp_readdirplus.dirent_array[i].handle;
                /* note: ignore return code... just use attrs of the symlink */
                sys_get_symlink_attr(fs_path, attr_array[i].link_target, 
                    &ref, credentials, &attr_array[i]);
            }
            /* clear allocated fields */
            PVFS_util_release_sys_attr(&attr_array[i]);
        }

        /* free memory */        
        free(resp_readdirplus.dirent_array);
        free(resp_readdirplus.stat_err_array);
        free(resp_readdirplus.attr_array);
    }

fs_readdir_exit:

    return ret;
}

/*
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
*/

int fs_get_diskfreespace(PVFS_credentials *credentials,
                         PVFS_size *free_bytes, 
                         PVFS_size *total_bytes)
{
    struct PVFS_sys_mntent *mntent = fs_get_mntent(0);
    PVFS_sysresp_statfs resp_statfs;
    int ret;

    if (free_bytes == NULL || total_bytes == NULL || credentials == NULL)
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
