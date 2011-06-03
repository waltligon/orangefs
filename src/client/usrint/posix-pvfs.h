
#ifndef POSIX_PVFS_H
#define POSIX_PVFS_H 1

/* pvfs_open */
int pvfs_open(const char *path, int flags, ...);

/* pvfs_open64 */
int pvfs_open64(const char *path, int flags, ...);

/* pvfs_openat */
int pvfs_openat(int dirfd, const char *path, int flags, ...);

/* pvfs_openat64 */
int pvfs_openat64(int dirfd, const char *path, int flags, ...);

int pvfs_creat(const char *path, mode_t mode, ...);

int pvfs_creat64(const char *path, mode_t mode, ...);

/* pvfs_unlink */
int pvfs_unlink (const char *path);

int pvfs_unlinkat (int dirfd, const char *path);

int pvfs_rename(const char *oldpath, const char *newpath);

int pvfs_renameat(int olddirfd, const char *oldpath,
                  int newdirfd, const char *newpath);

/* pvfs_read */
ssize_t pvfs_read( int fd, void *buf, size_t count );

/* pvfs_pread */
ssize_t pvfs_pread( int fd, void *buf, size_t count, off_t offset );

ssize_t pvfs_readv(int fd, const struct iovec *vector, int count);

/* pvfs_pread64 */
ssize_t pvfs_pread64( int fd, void *buf, size_t count, off64_t offset );

/* pvfs_write */
ssize_t pvfs_write( int fd, void *buf, size_t count );

/* pvfs_pwrite */
ssize_t pvfs_pwrite( int fd, void *buf, size_t count, off_t offset );

ssize_t pvfs_writev( int fd, const struct iovec *vector, int count );

/* pvfs_pwrite64 */
ssize_t pvfs_pwrite64( int fd, void *buf, size_t count, off64_t offset );

/* pvfs_lseek */
off_t pvfs_lseek(int fd, off_t offset, int whence);

/* pvfs_lseek64 */
off64_t pvfs_lseek64(int fd, off64_t offset, int whence);

int pvfs_truncate(const char *path, off_t length);

int pvfs_truncate64 (const char *path, off64_t length);

int pvfs_ftruncate (int fd, off_t length);

int pvfs_ftruncate64 (int fd, off64_t length);

/* pvfs_close */
int pvfs_close( int fd );

int pvfs_flush(int fd);

/* various flavors of stat */
int pvfs_stat(const char *path, struct stat *buf);

int pvfs_stat64(const char *path, struct stat64 *buf);

int pvfs_fstat(int fd, struct stat *buf);

int pvfs_fstat64(int fd, struct stat64 *buf);

int pvfs_fstatat(int fd, char *path, struct stat *buf, int flag);

int pvfs_fstatat64(int fd, char *path, struct stat64 *buf, int flag);

int pvfs_lstat(const char *path, struct stat *buf);

int pvfs_lstat64(const char *path, struct stat64 *buf);

int pvfs_dup(int oldfd);

int pvfs_dup2(int oldfd, int newfd);

int pvfs_chown (const char *path, uid_t owner, gid_t group);

int pvfs_fchown (int fd, uid_t owner, gid_t group);

int pvfs_fchownat(int fd, char *path, uid_t owner, gid_t group, int flag);

int pvfs_lchown (const char *path, uid_t owner, gid_t group);

int pvfs_chmod (const char *path, mode_t mode);

int pvfs_fchmod (int fd, mode_t mode);

int pvfs_fchmodat(int fd, char *path, mode_t mode, int flag);

/* this is not a Linux syscall, but its useful above */
int pvfs_lchmod (const char *path, mode_t mode);

int pvfs_mkdir (const char *path, mode_t mode);

int pvfs_mkdirat (int dirfd, const char *path, mode_t mode);

int pvfs_rmdir (const char *path);

ssize_t pvfs_readlink (const char *path, char *buf, size_t bufsiz);

int pvfs_readlinkat (int dirfd, const char *path, char *buf, size_t bufsiz);

ssize_t pvfs_symlink (const char *oldpath, const char *newpath);

int pvfs_symlinkat (const char *oldpath, int newdirfd, const char *newpath);

/* PVFS does not have hard links */
ssize_t pvfs_link (const char *oldpath, const char *newpath);

/* PVFS does not have hard links */
int pvfs_linkat (const char *oldpath, int newdirfd,
                 const char *newpath, int flags);

/* this reads exactly one dirent, count is ignored */
int pvfs_readdir(unsigned int fd, struct dirent *dirp, unsigned int count);

/* this reads multiple dirents, up to count */
int pvfs_getdents(unsigned int fd, struct dirent *dirp, unsigned int count);

int pvfs_access (const char * path, int mode);

int pvfs_faccessat (int dirfd, const char * path, int mode, int flags);

int pvfs_flock(int fd, int op);

int pvfs_fcntl(int fd, int cmd, ...);

/* sync all disk data */
void pvfs_sync(void );

/* sync file, but not dir it is in */
int pvfs_fsync(int fd);

/* does not sync file metadata */
int pvfs_fdatasync(int fd);

mode_t pvfs_umask(mode_t mask);

mode_t pvfs_getumask(void);

int pvfs_getdtablesize(void);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

