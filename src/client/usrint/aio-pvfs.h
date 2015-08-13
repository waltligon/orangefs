/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#ifndef AIO_PVFS_H
#define AIO_PVFS_H

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-sysint.h"
#include "posix-ops.h"
#include "aio.h"

int pvfs_aio_cancel(int fd, struct aiocb *aiocbp);

int pvfs_aio_error(const struct aiocb *aiocbp);

//int pvfs_aio_fsync(int op, struct aiocb *aiocbp);

int pvfs_aio_read(struct aiocb *aiocbp);

ssize_t pvfs_aio_return(struct aiocb *aiocbp);

//int pvfs_aio_suspend(const struct aiocb * const cblist[], int n,
//		const struct timespec *timeout);

int pvfs_aio_write(struct aiocb *aiocbp);

int pvfs_lio_listio(int mode, struct aiocb * const list[], int nent,
		    struct sigevent *sig);

int pvfs_aio_lio_callback(void *cdat, int status);

extern PVFS_error PVFS_iaio_close(pvfs_descriptor *pd,
                                  const PVFS_credential *credential,
                                  PVFS_sys_op_id *op_id, PVFS_hint hints,
                                  void *user_ptr);

extern PVFS_error PVFS_iaio_lseek(pvfs_descriptor *pd, off64_t offset,
                                  int whence, const PVFS_credential *credential,
                                  PVFS_sys_op_id *op_id, PVFS_hint hints,
                                  void *user_ptr);

extern PVFS_error PVFS_iaio_mkdir(const char *directory, const char *filename,
                                  PVFS_object_ref *pdir, mode_t mode,
                                  const PVFS_credential *credential,
                                  PVFS_sys_op_id *op_id, PVFS_hint hints,
                                  void *user_ptr);

extern PVFS_error PVFS_iaio_open(pvfs_descriptor **fildes, char *path,
                                 char *directory, char *filename, int flags,
                                 PVFS_hint file_creation_param, mode_t mode,
                                 pvfs_descriptor *pdir, 
                                 const PVFS_credential *credential,
                                 PVFS_sys_op_id *op_id, PVFS_hint hints,
                                 void *user_ptr);

extern PVFS_error PVFS_iaio_remove(const char *directory, const char *filename, 
                                   PVFS_object_ref *pdir, int dirflag, 
                                   const PVFS_credential *credential,
                                   PVFS_sys_op_id *op_id, PVFS_hint hints,
                                   void *user_ptr);

extern PVFS_error PVFS_iaio_rename(PVFS_object_ref *oldpdir, const char *olddir,
                                   const char *oldname, PVFS_object_ref *newpdir,
                                   const char *newdir, const char *newname,
                                   const PVFS_credential *credential, 
                                   PVFS_sys_op_id *op_id, PVFS_hint hints,
                                   void *user_ptr);

extern PVFS_error PVFS_iaio_symlink(const char *new_dir, const char *new_name,
                                    const char *target, PVFS_object_ref *pdir,
                                    const PVFS_credential *credential,
                                    PVFS_sys_op_id *op_id, PVFS_hint hints,
                                    void *user_ptr);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
