/*
 * (C) 2010-2011 Clemson University and Omnibond LLC
 *
 * See COPYING in top-level directory.
 */

 /*
  * Client Service - file system declarations
  */

#ifndef __FS_H
#define __FS_H

#include "pvfs2.h"

int fs_initialize(const char *tabfile,
                  char *error_msg,
                  size_t error_msg_len);

struct PVFS_sys_mntent *fs_get_mntent(PVFS_fs_id fs_id);

int fs_resolve_path(const char *local_path, 
                    char *fs_path,
                    size_t fs_path_max);

int fs_lookup(char *fs_path,
              PVFS_credentials *credentials,
              PVFS_handle *handle);

int fs_create(char *fs_path,
              PVFS_credentials *credentials,
              PVFS_handle *handle,
              unsigned int perms);

int fs_remove(char *fs_path,
              PVFS_credentials *credentials);

int fs_rename(char *old_path, 
              char *new_path,
              PVFS_credentials *credentials);

int fs_truncate(char *fs_path, 
                PVFS_size size,
                PVFS_credentials *credentials);

int fs_getattr(char *fs_path,
               PVFS_credentials *credentials,
               PVFS_sys_attr *attr);

int fs_setattr(char *fs_path,
               PVFS_sys_attr *attr,
               PVFS_credentials *credentials);

int fs_mkdir(char *fs_path,
             PVFS_credentials *credentials,
             PVFS_handle *handle,
             unsigned int perms);

int fs_io(enum PVFS_io_type io_type,
          char *fs_path,
          void *buffer,
          size_t buffer_len,
          uint64_t offset,
          PVFS_size *op_len,
          PVFS_credentials *credentials);

#define fs_read(fs_path, \
                buffer, \
                buffer_len, \
                offset, \
                read_len, \
                credentials)  fs_io(PVFS_IO_READ, fs_path, buffer, buffer_len, offset, read_len, credentials)


#define fs_write(fs_path, \
                 buffer, \
                 buffer_len, \
                 offset, \
                 write_len, \
                 credentials)  fs_io(PVFS_IO_WRITE, fs_path, buffer, buffer_len, offset, write_len, credentials)

int fs_flush(char *fs_path,
             PVFS_credentials *credentials);

/*
int fs_find_first_file(char *fs_path,
                       PVFS_ds_position *token,
                       PVFS_credential *credential,
                       char *filename,
                       size_t max_name_len);
*/

int fs_find_files(char *fs_path, 
                  PVFS_credentials *credentials,              
                  PVFS_ds_position *token,
                  int32_t incount,
                  int32_t *outcount,
                  char **filename_array,
                  PVFS_sys_attr *attr_array);

int fs_get_diskfreespace(PVFS_credentials *credentials,
                         PVFS_size *free_bytes, 
                         PVFS_size *total_bytes);

PVFS_fs_id fs_get_id(int fs_num);

char *fs_get_name(int fs_num);

int fs_finalize();

#endif