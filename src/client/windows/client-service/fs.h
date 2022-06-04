/*
 * (C) 2010-2022 Omnibond Systems, LLC
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
              PVFS_credential *credential,
              PVFS_handle *handle);

int fs_create(char *fs_path,
              PVFS_credential *credential,
              PVFS_handle *handle,
              unsigned int perms);

int fs_remove(char *fs_path,
              PVFS_credential *credential);

int fs_rename(char *old_path, 
              char *new_path,
              PVFS_credential *credential);

int fs_truncate(char *fs_path, 
                PVFS_size size,
                PVFS_credential *credential);

int fs_getattr(char *fs_path,
               PVFS_credential *credential,
               PVFS_sys_attr *attr);

int fs_setattr(char *fs_path,
               PVFS_sys_attr *attr,
               PVFS_credential *credential);

int fs_mkdir(char *fs_path,
             PVFS_credential *credential,
             PVFS_handle *handle,
             unsigned int perms);

int fs_io(enum PVFS_io_type io_type,
          char *fs_path,
          void *buffer,
          size_t buffer_len,
          uint64_t offset,
          PVFS_size *op_len,
          PVFS_credential *credential);

#define fs_read(fs_path, \
                buffer, \
                buffer_len, \
                offset, \
                read_len, \
                credential)  fs_io(PVFS_IO_READ, fs_path, buffer, buffer_len, offset, read_len, credential)


#define fs_write(fs_path, \
                 buffer, \
                 buffer_len, \
                 offset, \
                 write_len, \
                 credential)  fs_io(PVFS_IO_WRITE, fs_path, buffer, buffer_len, offset, write_len, credential)

int fs_io2(enum PVFS_io_type io_type,
           PVFS_object_ref object_ref,
           void *buffer,
           size_t buffer_len,
           uint64_t offset,
           PVFS_size *op_len,
           PVFS_credential *credential);

#define fs_read2(object_ref, \
                 buffer, \
                 buffer_len, \
                 offset, \
                 read_len, \
                 credential) \
                  fs_io2(PVFS_IO_READ, object_ref, buffer, buffer_len, offset, read_len, credential)

#define fs_write2(object_ref, \
                  buffer, \
                  buffer_len, \
                  offset, \
                  write_len, \
                  credential) \
                   fs_io2(PVFS_IO_WRITE, object_ref, buffer, buffer_len, offset, write_len, credential)

int fs_flush(char *fs_path,
             PVFS_credential *credential);

/*
int fs_find_first_file(char *fs_path,
                       PVFS_ds_position *token,
                       PVFS_credential *credential,
                       char *filename,
                       size_t max_name_len);
*/

int fs_find_files(char *fs_path, 
                  PVFS_credential *credential,              
                  PVFS_ds_position *token,
                  int32_t incount,
                  int32_t *outcount,
                  char **filename_array,
                  PVFS_sys_attr *attr_array);

int fs_get_diskfreespace(PVFS_credential *credential,
                         PVFS_size *free_bytes, 
                         PVFS_size *total_bytes);

PVFS_fs_id fs_get_id(int fs_num);

char *fs_get_name(int fs_num);

int fs_finalize();

#endif