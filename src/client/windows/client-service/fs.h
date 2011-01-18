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

int fs_rename(char *old_path, 
              char *new_path);

int fs_truncate(char *fs_path, 
                PVFS_size size);

int fs_getattr(char *fs_path,
               PVFS_sys_attr *attr);

int fs_setattr(char *fs_path,
               PVFS_sys_attr *attr);

int fs_mkdir(char *fs_path,
             PVFS_handle *handle);

int fs_io(enum PVFS_io_type io_type,
          char *fs_path,
          void *buffer,
          size_t buffer_len,
          uint64_t offset,
          PVFS_size *op_len);

#define fs_read(fs_path, \
                buffer, \
                buffer_len, \
                offset, \
                read_len)  fs_io(PVFS_IO_READ, fs_path, buffer, buffer_len, offset, read_len)


#define fs_write(fs_path, \
                 buffer, \
                 buffer_len, \
                 offset, \
                 write_len)  fs_io(PVFS_IO_WRITE, fs_path, buffer, buffer_len, offset, write_len)

int fs_flush(char *fs_path);

int fs_find_first_file(char *fs_path,
                       PVFS_ds_position *token,
                       char *filename,
                       size_t max_name_len);

int fs_find_next_file(char *fs_path, 
                      PVFS_ds_position *token,
                      char *filename,
                      size_t max_name_len);

int fs_get_diskfreespace(PVFS_size *free_bytes, 
                         PVFS_size *total_bytes);

int fs_finalize();

#endif