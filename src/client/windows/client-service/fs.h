/* Copyright (C) 2010 Omnibond, LLC          * 
 * Client Service - file system declarations */

#ifndef __FS_H
#define __FS_H

#include "pvfs2.h"

int fs_initialize();

struct PVFS_sys_mntent *fs_get_mntent(PVFS_fs_id fs_id);

int fs_resolve_path(const char *local_path, 
                    char *fs_path,
                    size_t fs_path_max);

int fs_lookup(char *fs_path,
              PVFS_handle *handle);

int fs_create(char *fs_path,
              PVFS_handle *handle);

int fs_remove(char *fs_path);

int fs_truncate(char *fs_path, 
                PVFS_size size);

int fs_getattr(char *fs_path,
               PVFS_sys_attr *attr);

int fs_mkdir(char *fs_path,
             PVFS_handle *handle);

int fs_read(char *fs_path, 
            void *buffer,
            size_t buffer_len,
            PVFS_offset offset,
            size_t *read_len);

int fs_write(char *fs_path,
             void *buffer,
             size_t buffer_len,
             PVFS_offset offset,
             size_t *write_len);

int fs_flush(char *fs_path);

int fs_read_first_file(char *fs_path,
                       PVFS_ds_position *token,
                       char *filename,
                       size_t max_name_len);

int fs_read_next_file(char *fs_path, 
                      PVFS_ds_position *token,
                      char *filename,
                      size_t max_name_len);

int fs_finalize();

#endif