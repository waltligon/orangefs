/*
 * (C) 2001 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS_AIO_H
#define __PVFS_AIO_H

#include "pvfs2.h"
#include "pvfs2-usrint.h"
#include "quicklist.h"
#include "gen-locks.h"
#include "posix-ops.h"

typedef PVFS_credentials PVFS_credential;

PVFS_error PVFS_aio_open(
    pvfs_descriptor **fildes,
    char *path,
    char *directory,
    char *filename,
    int flags,
    PVFS_hint file_creation_param,
    mode_t mode,
    pvfs_descriptor *pdir,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_iaio_open(
    pvfs_descriptor **fildes,
    char *path,
    char *directory,
    char *filename,
    int flags,
    PVFS_hint file_creation_param,
    mode_t mode,
    pvfs_descriptor *pdir,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_aio_rename(
    PVFS_object_ref *oldpdir,
    const char *olddir,
    const char *oldname,
    PVFS_object_ref *newpdir,
    const char *newdir,
    const char *newname,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_iaio_rename(
    PVFS_object_ref *oldpdir,
    const char *olddir,
    const char *oldname,
    PVFS_object_ref *newpdir,
    const char *newdir,
    const char *newname,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_iaio_mkdir(
    const char *directory,
    const char *filename,
    PVFS_object_ref *pdir,
    mode_t mode,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_aio_mkdir(
    const char *directory,
    const char *filename,
    PVFS_object_ref *pdir,
    mode_t mode,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_iaio_remove(
    const char *directory,
    const char *filename,
    PVFS_object_ref *pdir,
    int dirflag,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_aio_remove(
    const char *directory,
    const char *filename,
    PVFS_object_ref *pdir,
    int dirflag,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_iaio_symlink(
    const char *new_dir,
    const char *new_name,
    const char *target,
    PVFS_object_ref *pdir,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_aio_symlink(
    const char *new_dir,
    const char *new_name,
    const char *target,
    PVFS_object_ref *pdir,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_iaio_lseek(
    pvfs_descriptor *pd,
    off64_t offset,
    int whence,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_aio_lseek(
    pvfs_descriptor *pd,
    off64_t offset,
    int whence,
    const PVFS_credential *credential,
    PVFS_hint hints);

PVFS_error PVFS_iaio_close(
    pvfs_descriptor *pd,
    const PVFS_credential *credential,
    PVFS_sys_op_id *op_id,
    PVFS_hint hints,
    void *user_ptr);

PVFS_error PVFS_aio_close(
    pvfs_descriptor *pd,
    const PVFS_credential *credential,
    PVFS_hint hints);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

