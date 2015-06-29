/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines
 */

#ifndef POSIX_PVFS_H
#define POSIX_PVFS_H 1

/* define FD flags unique to PVFS here */
#define PVFS_FD_NOCACHE 0x10000

/* functions to check fd or path validity */
extern int pvfs_valid_path(const char *path);

extern int pvfs_valid_fd(int fd);

/* pvfs_open */
extern int pvfs_open(const char *path, int flags, ...);

/* pvfs_open64 */
extern int pvfs_open64(const char *path, int flags, ...);

/* pvfs_openat */
extern int pvfs_openat(int dirfd, const char *path, int flags, ...);

/* pvfs_openat64 */
extern int pvfs_openat64(int dirfd, const char *path, int flags, ...);

extern int pvfs_creat(const char *path, mode_t mode);

extern int pvfs_creat64(const char *path, mode_t mode);

/* pvfs_unlink */
extern int pvfs_unlink (const char *path);

extern int pvfs_unlinkat (int dirfd, const char *path, int flags);

extern int pvfs_rename(const char *oldpath, const char *newpath);

extern int pvfs_renameat(int olddirfd, const char *oldpath,
                  int newdirfd, const char *newpath);

/* pvfs_read */
extern ssize_t pvfs_read( int fd, void *buf, size_t count );

/* pvfs_pread */
extern ssize_t pvfs_pread( int fd, void *buf, size_t count, off_t offset );

extern ssize_t pvfs_readv(int fd, const struct iovec *vector, int count);

/* pvfs_pread64 */
extern ssize_t pvfs_pread64( int fd, void *buf, size_t count, off64_t offset );

/* pvfs_write */
extern ssize_t pvfs_write( int fd, const void *buf, size_t count );

/* pvfs_pwrite */
extern ssize_t pvfs_pwrite( int fd, const void *buf, size_t count, off_t offset );

extern ssize_t pvfs_writev( int fd, const struct iovec *vector, int count );

/* pvfs_pwrite64 */
extern ssize_t pvfs_pwrite64( int fd, const void *buf, size_t count, off64_t offset );

/* pvfs_lseek */
extern off_t pvfs_lseek(int fd, off_t offset, int whence);

/* pvfs_lseek64 */
extern off64_t pvfs_lseek64(int fd, off64_t offset, int whence);

extern int pvfs_truncate(const char *path, off_t length);

extern int pvfs_truncate64 (const char *path, off64_t length);

extern int pvfs_fallocate(int fd, off_t offset, off_t length);

extern int pvfs_ftruncate (int fd, off_t length);

extern int pvfs_ftruncate64 (int fd, off64_t length);

/* pvfs_close */
extern int pvfs_close( int fd );

extern int pvfs_flush(int fd);

/* various flavors of stat */
extern int pvfs_stat(const char *path, struct stat *buf);

extern int pvfs_stat64(const char *path, struct stat64 *buf);

extern int pvfs_stat_mask(const char *path, struct stat *buf, uint32_t mask);

extern int pvfs_fstat(int fd, struct stat *buf);

extern int pvfs_fstat64(int fd, struct stat64 *buf);

extern int pvfs_fstatat(int fd, const char *path, struct stat *buf, int flag);

extern int pvfs_fstatat64(int fd, const char *path, struct stat64 *buf, int flag);

extern int pvfs_fstat_mask(int fd, struct stat *buf, uint32_t mask);

extern int pvfs_lstat(const char *path, struct stat *buf);

extern int pvfs_lstat64(const char *path, struct stat64 *buf);

extern int pvfs_lstat_mask(const char *path, struct stat *buf, uint32_t mask);

#ifdef __USE_GLIBC__
extern int pvfs_utimensat(int dirfd,
                          const char *path,
                          const struct timespec times[2],
                          int flags);

extern int pvfs_futimens(int fd, const struct timespec times[2]);
#endif

extern int pvfs_futimesat(int dirfd, const char *path, const struct timeval times[2]);

extern int pvfs_utimes(const char *path, const struct timeval times[2]);

extern int pvfs_utime(const char *path, const struct utimbuf *buf);

extern int pvfs_futimes(int fd, const struct timeval times[2]);

extern int pvfs_dup(int oldfd);

extern int pvfs_dup2(int oldfd, int newfd);

extern int pvfs_dup3(int oldfd, int newfd, int flags);

extern int pvfs_chown (const char *path, uid_t owner, gid_t group);

extern int pvfs_fchown (int fd, uid_t owner, gid_t group);

extern int pvfs_fchownat(int fd, const char *path, uid_t owner, gid_t group, int flag);

extern int pvfs_lchown (const char *path, uid_t owner, gid_t group);

extern int pvfs_chmod (const char *path, mode_t mode);

extern int pvfs_fchmod (int fd, mode_t mode);

extern int pvfs_fchmodat(int fd, const char *path, mode_t mode, int flag);

extern int pvfs_mkdir (const char *path, mode_t mode);

extern int pvfs_mkdirat (int dirfd, const char *path, mode_t mode);

extern int pvfs_rmdir (const char *path);

extern ssize_t pvfs_readlink (const char *path, char *buf, size_t bufsiz);

extern ssize_t pvfs_readlinkat (int dirfd, const char *path, char *buf, size_t bufsiz);

extern int pvfs_symlink (const char *oldpath, const char *newpath);

extern int pvfs_symlinkat (const char *oldpath, int newdirfd, const char *newpath);

/* PVFS does not have hard links */
extern int pvfs_link (const char *oldpath, const char *newpath);

/* PVFS does not have hard links */
extern int pvfs_linkat (int olddirfd, const char *oldpath,
                 int newdirfd, const char *newpath, int flags);

/* this reads exactly one dirent, count is ignored */
extern int pvfs_readdir(unsigned int fd, struct dirent *dirp, unsigned int count);

/* this reads multiple dirents, up to count */
extern int pvfs_getdents(unsigned int fd, struct dirent *dirp, unsigned int count);

extern int pvfs_getdents64(unsigned int fd, struct dirent64 *dirp, unsigned int count);

extern int pvfs_access (const char * path, int mode);

extern int pvfs_faccessat (int dirfd, const char * path, int mode, int flags);

extern int pvfs_flock(int fd, int op);

extern int pvfs_fcntl(int fd, int cmd, ...);

/* sync all disk data */
extern void pvfs_sync(void );

/* sync file, but not dir it is in */
extern int pvfs_fsync(int fd);

/* does not sync file metadata */
extern int pvfs_fdatasync(int fd);

extern int pvfs_fadvise(int fd, off_t offset, off_t len, int advice);

extern int pvfs_fadvise64(int fd, off64_t offset, off64_t len, int advice);

extern int pvfs_statfs(const char *path, struct statfs *buf);

extern int pvfs_statfs64(const char *path, struct statfs64 *buf);

extern int pvfs_fstatfs(int fd, struct statfs *buf);

extern int pvfs_fstatfs64(int fd, struct statfs64 *buf);

extern int pvfs_statvfs(const char *path, struct statvfs *buf);

extern int pvfs_fstatvfs(int fd, struct statvfs *buf);

extern int pvfs_mknod(const char *path, mode_t mode, dev_t dev);

extern int pvfs_mknodat(int dirfd, const char *path, mode_t mode, dev_t dev);

extern ssize_t pvfs_sendfile(int outfd, int infd, off_t *offset, size_t count);

extern ssize_t pvfs_sendfile64(int outfd, int infd, off64_t *offset, size_t count);

extern int pvfs_setxattr(const char *path, const char *name,
                          const void *value, size_t size, int flags);

extern int pvfs_lsetxattr(const char *path, const char *name,
                          const void *value, size_t size, int flags);

extern int pvfs_fsetxattr(int fd, const char *name,
                          const void *value, size_t size, int flags);

extern ssize_t pvfs_getxattr(const char *path, const char *name,
                             void *value, size_t size);

extern ssize_t pvfs_lgetxattr(const char *path, const char *name,
                              void *value, size_t size);

extern ssize_t pvfs_fgetxattr(int fd, const char *name,
                              void *value, size_t size);

extern ssize_t pvfs_atomicxattr(const char *path, const char *name,
                                void *value, size_t valsize, void *response,
                                size_t respsize, int flags, int opcode);

extern ssize_t pvfs_latomicxattr(const char *path, const char *name,
                                void *value, size_t valsize, void *response,
                                size_t respsize, int flags, int opcode);

extern ssize_t pvfs_fatomicxattr(int fd, const char *name,
                                void *value, size_t valsize, void *response,
                                size_t respsize, int flags, int opcode);

extern ssize_t pvfs_listxattr(const char *path, char *list, size_t size);

extern ssize_t pvfs_llistxattr(const char *path, char *list, size_t size);

extern ssize_t pvfs_flistxattr(int fd, char *list, size_t size);

extern int pvfs_removexattr(const char *path, const char *name);

extern int pvfs_lremovexattr(const char *path, const char *name);

extern int pvfs_fremovexattr(int fd, const char *name);

extern int pvfs_chdir(const char *path);

extern int pvfs_fchdir(int fd);

extern int pvfs_cwd_init(int expand);

extern char *pvfs_getcwd(char *buf, size_t size);

extern char *pvfs_get_current_dir_name(void);

extern char *pvfs_getwd(char *buf);

extern mode_t pvfs_umask(mode_t mask);

extern mode_t pvfs_getumask(void);

extern int pvfs_getdtablesize(void);

extern void *pvfs_mmap(void *start, size_t length, int prot, int flags,
                int fd, off_t offset);

extern int pvfs_munmap(void *start, size_t length);

extern int pvfs_msync(void *start, size_t length, int flags);

/* these are defined in acl.c and don't really need */
/* a PVFS implementation */
#if 0
extern int pvfs_acl_delete_def_file(const char *path_p);

extern acl_t pvfs_acl_get_fd(int fd);

extern acl_t pvfs_acl_get_file(const char *path_p, acl_type_t type);

extern int pvfs_acl_set_fd(int fd, acl_t acl);

extern int pvfs_acl_set_file(const char *path_p, acl_type_t type, acl_t acl);
#endif

int pvfs_getfscreatecon(security_context_t *con);

int pvfs_getfilecon(const char *path, security_context_t *con);

int pvfs_lgetfilecon(const char *path, security_context_t *con);

int pvfs_fgetfilecon(int fd, security_context_t *con);

int pvfs_setfscreatecon(security_context_t con);

int pvfs_setfilecon(const char *path, security_context_t con);

int pvfs_lsetfilecon(const char *path, security_context_t con);

int pvfs_fsetfilecon(int fd, security_context_t con);


#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

