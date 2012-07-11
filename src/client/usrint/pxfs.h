/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PXFS user interface routines
 */

#ifndef POSIX_PXFS_H
#define POSIX_PXFS_H 1

#include "usrint.h"
#include "posix-ops.h"
#include "openfile-util.h"
#include "iocommon.h"
#include "aiocommon.h"

typedef int (*pxfs_cb)(void *cdat, int status);

/* define FD flags unique to PXFS here */
#define PXFS_FD_NOCACHE 0x10000

/*
 * All of these functions return either 0 for success or -1 for error 
 * System calls that return a useful value have and added argument for
 * for outputs.  All of these functions are asynchronous except for
 * those marked as "Local" functions.  All async functions have two
 * arguments for a callback function and a void pointer which is passed
 * to the function.  Callbacks should return 0 for sucess or -1 for
 * error.
 */

/*
 * Open functions have two optional arguments - an in mode arg that is
 * used if the O_CREAT is set and a hint arg that is used if the O_HINT
 * flag is set.  Creat functions also take the O_HINT flag and optional
 * argument.
 */

extern int pxfs_open(const char *path, int flags, int *fd,
                     pxfs_cb cb, void *cdat, ...);

extern int pxfs_open64(const char *path, int flags, int *fd,
                       pxfs_cb cb, void *cdat, ...);

extern int pxfs_openat(int dirfd, const char *path, int flags, int *fd,
                       pxfs_cb cb, void *cdat, ...);

extern int pxfs_openat64(int dirfd, const char *path, int flags, int *fd,
                         pxfs_cb cb, void *cdat, ...);

extern int pxfs_creat(const char *path, mode_t mode, int *fd,
                      pxfs_cb cb, void *cdat, ...);

extern int pxfs_creat64(const char *path, mode_t mode, int *fd,
                        pxfs_cb cb, void *cdat, ...);

extern int pxfs_unlink (const char *path, pxfs_cb cb, void *cdat);

extern int pxfs_unlinkat (int dirfd, const char *path, int flags,
                          pxfs_cb cb, void *cdat);

extern int pxfs_rename(const char *oldpath, const char *newpath,
                       pxfs_cb cb, void *cdat);

extern int pxfs_renameat(int olddirfd, const char *oldpath, int newdirfd,
                         const char *newpath, pxfs_cb cb, void *cdat);

extern int pxfs_read(int fd, void *buf, size_t count, ssize_t *bcnt,
                     pxfs_cb cb, void *cdat);

extern int pxfs_pread(int fd, void *buf, size_t count, off_t offset,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_readv(int fd, const struct iovec *vector, int count,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_pread64(int fd, void *buf, size_t count, off64_t offset,
                        ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_write(int fd, const void *buf, size_t count,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_pwrite(int fd, const void *buf, size_t count, off_t offset,
                       ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_writev(int fd, const struct iovec *vector, int count,
                       ssize_t *bcnt , pxfs_cb cb, void *cdat);

extern int pxfs_pwrite64(int fd, const void *buf, size_t count,
                         off64_t offset, ssize_t *bcnt,
                         pxfs_cb cb, void *cdat);

/* Local Function */
extern int pxfs_lseek(int fd, off_t offset, int whence, off_t *offset_out);

/* Local Function */
extern int pxfs_lseek64(int fd, off64_t offset, int whence, off_t *offset_out);

extern int pxfs_truncate(const char *path, off_t length,
                         pxfs_cb cb, void *cdat);

extern int pxfs_truncate64 (const char *path, off64_t length,
                            pxfs_cb cb, void *cdat);

extern int pxfs_fallocate(int fd, off_t offset, off_t length,
                          pxfs_cb cb, void *cdat);

extern int pxfs_ftruncate (int fd, off_t length, pxfs_cb cb, void *cdat);

extern int pxfs_ftruncate64 (int fd, off64_t length, pxfs_cb cb, void *cdat);

/* If caching could cause writeback */
extern int pxfs_close( int fd , pxfs_cb cb, void *cdat);

extern int pxfs_flush(int fd, pxfs_cb cb, void *cdat);

extern int pxfs_stat(const char *path, struct stat *buf,
                     pxfs_cb cb, void *cdat);

extern int pxfs_stat64(const char *path, struct stat64 *buf,
                       pxfs_cb cb, void *cdat);

extern int pxfs_stat_mask(const char *path, struct stat *buf,
                          uint32_t mask, pxfs_cb cb, void *cdat);

extern int pxfs_fstat(int fd, struct stat *buf, pxfs_cb cb, void *cdat);

extern int pxfs_fstat64(int fd, struct stat64 *buf, pxfs_cb cb, void *cdat);

extern int pxfs_fstatat(int fd, const char *path, struct stat *buf,
                        int flag, pxfs_cb cb, void *cdat);

extern int pxfs_fstatat64(int fd, const char *path, struct stat64 *buf,
                          int flag, pxfs_cb cb, void *cdat);

extern int pxfs_fstat_mask(int fd, struct stat *buf, uint32_t mask,
                           pxfs_cb cb, void *cdat);

extern int pxfs_lstat(const char *path, struct stat *buf,
                      pxfs_cb cb, void *cdat);

extern int pxfs_lstat64(const char *path, struct stat64 *buf,
                        pxfs_cb cb, void *cdat);

extern int pxfs_lstat_mask(const char *path, struct stat *buf, uint32_t mask,
                           pxfs_cb cb, void *cdat);

extern int pxfs_futimesat(int dirfd, const char *path,
                          const struct timeval times[2],
                          pxfs_cb cb, void *cdat);

extern int pxfs_utimes(const char *path, const struct timeval times[2],
                       pxfs_cb cb, void *cdat);

extern int pxfs_utime(const char *path, const struct utimbuf *buf,
                      pxfs_cb cb, void *cdat);

extern int pxfs_futimes(int fd, const struct timeval times[2],
                        pxfs_cb cb, void *cdat);

/* Local function */
extern int pxfs_dup(int oldfd, int *newfd);

/* Local function */
extern int pxfs_dup2(int oldfd, int newfd);

extern int pxfs_chown (const char *path, uid_t owner, gid_t group,
                       pxfs_cb cb, void *cdat);

extern int pxfs_fchown (int fd, uid_t owner, gid_t group,
                        pxfs_cb cb, void *cdat);

extern int pxfs_fchownat(int fd, const char *path, uid_t owner, gid_t group,
                         int flag, pxfs_cb, void *cdat);

extern int pxfs_lchown (const char *path, uid_t owner, gid_t group,
                        pxfs_cb cb, void *cdat);

extern int pxfs_chmod (const char *path, mode_t mode, pxfs_cb cb, void *cdat);

extern int pxfs_fchmod (int fd, mode_t mode, pxfs_cb cb, void *cdat);

extern int pxfs_fchmodat(int fd, const char *path, mode_t mode, int flag,
                         pxfs_cb cb, void *cdat);

extern int pxfs_mkdir (const char *path, mode_t mode, pxfs_cb cb, void *cdat);

extern int pxfs_mkdirat (int dirfd, const char *path, mode_t mode,
                         pxfs_cb cb, void *cdat);

extern int pxfs_rmdir (const char *path, pxfs_cb cb, void *cdat);

extern int pxfs_readlink (const char *path, char *buf, size_t bufsiz,
                         ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_readlinkat (int dirfd, const char *path, char *buf,
                            size_t bufsiz, ssize_t *bcnt,
                            pxfs_cb cb, void *cdat);

extern int pxfs_symlink (const char *oldpath, const char *newpath,
                         pxfs_cb cb, void *cdat);

extern int pxfs_symlinkat (const char *oldpath, int newdirfd,
                           const char *newpath, pxfs_cb cb, void *cdat);

/* PVFS does not have hard links */
extern int pxfs_link (const char *oldpath, const char *newpath,
                      pxfs_cb cb, void *cdat);

/* PVFS does not have hard links */
extern int pxfs_linkat (int olddirfd, const char *oldpath,
                        int newdirfd, const char *newpath, int flags,
                        pxfs_cb cb, void *cdat);

/* this reads exactly one dirent, count is ignored */
extern int pxfs_readdir(unsigned int fd, struct dirent *dirp,
                        unsigned int count, pxfs_cb, void *cdat);

/* this reads multiple dirents, up to count */
extern int pxfs_getdents(unsigned int fd, struct dirent *dirp,
                         unsigned int count, pxfs_cb, void *cdat);

extern int pxfs_getdents64(unsigned int fd, struct dirent64 *dirp,
                           unsigned int count, pxfs_cb, void *cdat);

extern int pxfs_access (const char * path, int mode, pxfs_cb, void *cdat);

extern int pxfs_faccessat (int dirfd, const char * path, int mode, int flags,
                           pxfs_cb cb, void *cdat);

extern int pxfs_flock(int fd, int op, pxfs_cb, void *cdat);

/*
 * Fcntl takes optional argumends depending on the cmd argument
 */

extern int pxfs_fcntl(int fd, int cmd, pxfs_cb, void *cdat, ...);

/* sync all disk data */
extern int pxfs_sync(pxfs_cb, void *cdat);

/* sync file, but not dir it is in */
extern int pxfs_fsync(int fd, pxfs_cb cb, void *cdat);

/* does not sync file metadata */
extern int pxfs_fdatasync(int fd, pxfs_cb cb, void *cdat);

extern int pxfs_fadvise(int fd, off_t offset, off_t len, int advice,
                        pxfs_cb cb, void *cdat);

extern int pxfs_fadvise64(int fd, off64_t offset, off64_t len, int advice,
                          pxfs_cb cb, void *cdat);

extern int pxfs_statfs(const char *path, struct statfs *buf,
                       pxfs_cb cb, void *cdat);

extern int pxfs_statfs64(const char *path, struct statfs64 *buf,
                         pxfs_cb cb, void *cdat);

extern int pxfs_fstatfs(int fd, struct statfs *buf, pxfs_cb cb, void *cdat);

extern int pxfs_fstatfs64(int fd, struct statfs64 *buf,
                          pxfs_cb cb, void *cdat);

extern int pxfs_statvfs(const char *path, struct statvfs *buf,
                        pxfs_cb cb, void *cdat);

extern int pxfs_fstatvfs(int fd, struct statvfs *buf,
                         pxfs_cb cb, void *cdat);

extern int pxfs_mknod(const char *path, mode_t mode, dev_t dev,
                      pxfs_cb cb, void *cdat);

extern int pxfs_mknodat(int dirfd, const char *path, mode_t mode, dev_t dev,
                        pxfs_cb cb, void *cdat);

extern int pxfs_sendfile(int outfd, int infd, off_t *offset,
                         size_t count, ssize_t *bcnt,
                         pxfs_cb cb, void *cdat);

extern int pxfs_sendfile64(int outfd, int infd, off64_t *offset,
                           size_t count, ssize_t *bcnt,
                           pxfs_cb cb, void *cdat);

extern int pxfs_setxattr(const char *path, const char *name,
                          const void *value, size_t size, int flags,
                          pxfs_cb cb, void *cdat);

extern int pxfs_lsetxattr(const char *path, const char *name,
                          const void *value, size_t size, int flags,
                          pxfs_cb cb, void *cdat);

extern int pxfs_fsetxattr(int fd, const char *name,
                          const void *value, size_t size, int flags,
                          pxfs_cb cb, void *cdat);

extern int pxfs_getxattr(const char *path, const char *name,
                         void *value, size_t size, ssize_t *bcnt,
                         pxfs_cb cb, void *cdat);

extern int pxfs_lgetxattr(const char *path, const char *name,
                          void *value, size_t size, ssize_t *bcnt,
                          pxfs_cb cb, void *cdat);

extern int pxfs_fgetxattr(int fd, const char *name,
                          void *value, size_t size, ssize_t *bcnt,
                          pxfs_cb cb, void *cdat);

extern int pxfs_listxattr(const char *path, char *list, size_t size,
                          ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_llistxattr(const char *path, char *list, size_t size,
                           ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_flistxattr(int fd, char *list, size_t size, ssize_t *bcnt,
                           pxfs_cb cb, void *cdat);

extern int pxfs_removexattr(const char *path, const char *name,
                            pxfs_cb cb, void *cdat);

extern int pxfs_lremovexattr(const char *path, const char *name,
                             pxfs_cb cb, void *cdat);

extern int pxfs_fremovexattr(int fd, const char *name,
                             pxfs_cb cb, void *cdat);

/* Local functions */

extern int pxfs_chdir(const char *path);

extern int pxfs_fchdir(int fd);

extern int pxfs_cwd_init(const char *buf, size_t size);

extern char *pxfs_getcwd(char *buf, size_t size);

extern char *pxfs_get_current_dir_name(void);

extern char *pxfs_getwd(char *buf);

extern mode_t pxfs_umask(mode_t mask);

extern mode_t pxfs_getumask(void);

extern int pxfs_getdtablesize(void);

/* mmap functions */

#ifdef PXFS_MMAP
extern void *pxfs_mmap(void *start, size_t length, int prot, int flags,
                int fd, off_t offset);

extern int pxfs_munmap(void *start, size_t length);

extern int pxfs_msync(void *start, size_t length, int flags);
#endif

/* acl functions */

#ifdef PXFS_ACLS
extern int pxfs_acl_delete_def_file(const char *path_p,
                                    pxfs_cb cb, void *cdat);

extern int pxfs_acl_get_fd(int fd, acl_t *act,
                           pxfs_cb cb, void *cdat);

extern int pxfs_acl_get_file(const char *path_p, acl_type_t type,
                             acl_t *acl, pxfs_cb cb, void *cdat);

extern int pxfs_acl_set_fd(int fd, acl_t acl, pxfs_cb cb, void *cdat);

extern int pxfs_acl_set_file(const char *path_p, acl_type_t type, acl_t acl,
                             pxfs_cb cb, void *cdat);
#endif


#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

