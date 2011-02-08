#include <usrint.h>

static mode_t mask_val = 0022; /* implements umask for pvfs library */

/* pvfs_open */
int pvfs_open(const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    char *newpath;
    pvfs_descriptor *pd;

    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    else
        mode = 0777;
    if (flags & O_HINTS)
        hints = va_arg(ap, PVFS_hint);
    else
        hints = PVFS_HINT_NULL;
    va_end(ap);

    /* fully qualify pathname */
    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, flags, hints, mode, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    return pd->fd;
}

/* pvfs_open64 */
int pvfs_open64(const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;

    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    else
        mode = 0777;
    if (flags & O_HINTS)
        hints = va_arg(ap, PVFS_hint);
    else
        hints = PVFS_HINT_NULL;
    va_end(ap);
    flags |= O_LARGEFILE;
    return pvfs_open(path, flags, mode);
}

/* pvfs_openat */
int pvfs_openat(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    pvfs_descriptor *dpd, *fpd;

    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    else
        mode = 0777;
    if (flags & O_HINTS)
        hints = va_arg(ap, PVFS_hint);
    else
        hints = PVFS_HINT_NULL;
    va_end(ap);
    if (path[0] == '/')
    {
        return pvfs_open(path, flags, mode);
    }
    else
    {
        dpd = pvfs_find_descriptor(dirfd);
        fpd = iocommon_open(path, flags, hints, mode, &dpd->pvfs_ref);
        return fpd->fd;
    }
        
}

/* pvfs_openat64 */
int pvfs_openat64(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    pvfs_descriptor *pd;

    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    else
        mode = 0777;
    if (flags & O_HINTS)
        hints = va_arg(ap, PVFS_hint);
    else
        hints = PVFS_HINT_NULL;
    va_end(ap);
    flags |= O_LARGEFILE;
    return pvfs_openat(dirfd, path, flags, mode);
}

int pvfs_creat(const char *path, mode_t mode, ...)
{
}

int pvfs_creat64(const char *path, mode_t mode, ...)
{
}

/* pvfs_unlink */
int pvfs_unlink (const char *path)
{
    return iocommon_unlink(path);
}

int pvfs_unlinkat (int dirfd, const char *path)
{
    pvfs_descriptor *pd, *pd2;
    if (path[0] == '/' || dirfd == AT_FDCWD)
        pvfs_unlink(path);
    else
    {
        int flags = O_RDONLY;
        pd = pvfs_find_descriptor(dirfd);
        /* not sure what to do with this ... */
        return -1;
    }
}

int pvfs_rename(const char *oldpath, const char *newpath)
{
    int rc;
    char *absoldpath, *absnewpath;

    absoldpath = pvfs_qualify_path(oldpath);
    absnewpath = pvfs_qualify_path(newpath);
    rc = iocommon_rename(NULL, absoldpath, NULL, absnewpath);
    if (oldpath != absoldpath)
    {
        free(absoldpath);
    }
    if (newpath != absnewpath)
    {
        free(absnewpath);
    }
    return rc;
}

int pvfs_renameat(int olddirfd, const char *oldpath,
                  int newdirfd, const char *newpath)
{
    int rc;
    pvfs_descriptor *olddes, *newdes;
    char *absoldpath, *absnewpath;

    if (oldpath[0] != '/')
    {
        olddes = pvfs_find_descriptor(olddirfd);
        absoldpath = (char *)oldpath;
    }
    else
    {
        olddes = NULL;
        absoldpath = pvfs_qualify_path(oldpath);
    }
    if (oldpath[0] != '/')
    {
        newdes = pvfs_find_descriptor(newdirfd);
        absnewpath = (char *)newpath;
    }
    else
    {
        newdes = NULL;
        absnewpath = pvfs_qualify_path(newpath);
    }
    rc = iocommon_rename(olddes, absoldpath, newdes, absnewpath);
    if (oldpath != absoldpath)
    {
        free(absoldpath);
    }
    if (newpath != absnewpath)
    {
        free(absnewpath);
    }
    return rc;
}

/* pvfs_read */
ssize_t pvfs_read( int fd, void *buf, size_t count )
{
    int rc;
    pvfs_descriptor *pd = pvfs_find_descriptor(fd);
    rc = pvfs_pread64(fd, buf, count, pd->file_pointer);
    if (rc > 0)
        pd->file_pointer += rc;
    return rc;
}

/* pvfs_pread */
ssize_t pvfs_pread( int fd, void *buf, size_t count, off_t offset )
{
    return pvfs_pread_64(fd, buf, count, (off64_t) offset);
}

ssize_t pvfs_readv(int fd, const struct iovec *vector, int count)
{
}

/* pvfs_pread64 */
ssize_t pvfs_pread64( int fd, void *buf, size_t count, off64_t offset )
{
    int rc;
    pvfs_descriptor* pd;
    PVFS_Request freq;

    memset(&freq, 0, sizeof(freq));

    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (pd == NULL) {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    /* Ensure descriptor is available for read access */
    if (O_WRONLY & pd->flags) {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    rc = PVFS_Request_contiguous(count, PVFS_BYTE, &freq);

    iocommon_readorwrite(PVFS_IO_READ, pd, offset, buf, PVFS_BYTE, freq, count);

    PVFS_Request_free(&freq);

    return count;
}

/* pvfs_write */
ssize_t pvfs_write( int fd, void *buf, size_t count )
{
    int rc;
    pvfs_descriptor *pd = pvfs_find_descriptor(fd);
    rc = pvfs_pwrite64(fd, buf, count, pd->file_pointer);
    if (rc > 0)
        pd->file_pointer += rc;
    return rc;
}

/* pvfs_pwrite */
ssize_t pvfs_pwrite( int fd, void *buf, size_t count, off_t offset )
{
    return pvfs_pwrite64(fd, buf, count, (off64_t) offset);
}

ssize_t pvfs_writev( int fd, const struct iovec *vector, int count )
{
}

/* pvfs_pwrite64 */
ssize_t pvfs_write64( int fd, void *buf, size_t count, off64_t offset )
{
    int rc;
    pvfs_descriptor* pd;
    PVFS_Request freq;

    memset(&freq, 0, sizeof(freq));

    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (pd == NULL) {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    /* Ensure descriptor is available for read access */
    if (O_RDONLY & pd->flags) {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    rc = PVFS_Request_contiguous(count, PVFS_BYTE, &freq);

    iocommon_readorwrite(PVFS_IO_WRITE, pd, offset, buf, PVFS_BYTE, freq, count);

    PVFS_Request_free(&freq);

    return count;
}

/* pvfs_lseek */
off_t pvfs_lseek(int fd, off_t offset, int whence)
{
    return (off_t) pvfs_lseek64(fd, (off64_t)offset, whence);
}

/* pvfs_lseek64 */
off64_t pvfs_lseek64(int fd, off64_t offset, int whence)
{
    pvfs_descriptor* pd;

    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (pd == 0) {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    iocommon_lseek(pd, offset, 1, whence);

    return pd->file_pointer;
}

int pvfs_truncate(const char *path, off_t length)
{
    return pvfs_truncate64(path, (off64_t) length);
}

int pvfs_truncate64 (const char *path, off64_t length)
{
    int rc;
    pvfs_descriptor *pd;

    pd = iocommon_open(path, O_WRONLY, PVFS_HINT_NULL, 0 , NULL);
    rc = iocommon_truncate(pd->pvfs_ref, length);
    pvfs_close(pd->fd);
    return rc;
}

int pvfs_ftruncate (int fd, off_t length)
{
    return pvfs_ftruncate64(fd, (off64_t) length);
}

int pvfs_ftruncate64 (int fd, off64_t length)
{
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    return iocommon_truncate(pd->pvfs_ref, length);
}

/* pvfs_close */
int pvfs_close( int fd )
{
    pvfs_descriptor* pd;

    pd = pvfs_find_descriptor(fd);
    if (pd == 0) {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    /* flush buffers */
    pvfs_flush(fd);

    /* free buffers */
    if (pd->buf)
        free(pd->buf);

    /* free descriptor */
    pvfs_free_descriptor(fd);

    return PVFS_FD_SUCCESS;
}

int pvfs_flush(int fd)
{
    pvfs_descriptor* pd;
    int rc;

#ifdef DEBUG
    pvfs_debug("in pvfs_flush(%ld)\n", fd);
#endif

    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (pd == 0) {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    /* write any buffered data to the server */
    if (pd->buf && pd->dirty)
    {
        pd->file_pointer = pd->buf_off;
        rc = pvfs_write(fd, pd->buf,
                (pd->buftotal < pd->bufsize) ?  pd->buftotal : pd->bufsize);
        pd->dirty = 0;
    }

    /* tell the server to flush data to disk */
    rc = iocommon_fsync(pd);
    if (rc == 0) {
        return PVFS_FD_SUCCESS;
    }
    else {
        errno = EINVAL;
        return PVFS_FD_FAILURE;
    }
}

/* various flavors of stat */
int pvfs_stat(const char *path, struct stat *buf)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    iocommon_stat(pd, buf);
    pvfs_close(pd->fd);
}

int pvfs_stat64(const char *path, struct stat64 *buf)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    iocommon_stat64(pd, buf);
    pvfs_close(pd->fd);
}

int pvfs_fstat(int fd, struct stat *buf)
{
    pvfs_descriptor *pd;
    pd = pvfs_find_descriptor(fd);
    iocommon_stat(pd, buf);
}

int pvfs_fstat64(int fd, struct stat64 *buf)
{
    pvfs_descriptor *pd;
    pd = pvfs_find_descriptor(fd);
    iocommon_stat64(pd, buf);
}

int pvfs_fstatat(int fd, char *path, struct stat *buf, int flag)
{
    pvfs_descriptor *pd, *pd2;
    if (path[0] == '/' || fd == AT_FDCWD)
        if (flag & AT_SYMLINK_NOFOLLOW)
            pvfs_lstat(path, buf);
        else
            pvfs_stat(path, buf);
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
            flags |= O_NOFOLLOW;
        pd = pvfs_find_descriptor(fd);
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        iocommon_stat(pd2, buf);
        pvfs_close(pd2->fd);
    }
}

int pvfs_fstatat64(int fd, char *path, struct stat64 *buf, int flag)
{
    pvfs_descriptor *pd, *pd2;
    if (path[0] == '/' || fd == AT_FDCWD)
        if (flag & AT_SYMLINK_NOFOLLOW)
            pvfs_lstat64(path, buf);
        else
            pvfs_stat64(path, buf);
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
            flags |= O_NOFOLLOW;
        pd = pvfs_find_descriptor(fd);
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        iocommon_stat64(pd2, buf);
        pvfs_close(pd2->fd);
    }
}

int pvfs_lstat(const char *path, struct stat *buf)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
       free(newpath);
    }
    iocommon_stat(pd, buf);
    pvfs_close(pd->fd);
}

int pvfs_lstat64(const char *path, struct stat64 *buf)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    iocommon_stat64(pd, buf);
    pvfs_close(pd->fd);
}

int pvfs_dup(int oldfd)
{
    return pvfs_dup_descriptor(oldfd, -1);
}

int pvfs_dup2(int oldfd, int newfd)
{
    return pvfs_dup_descriptor(oldfd, newfd);
}

int pvfs_chown (const char *path, uid_t owner, gid_t group)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    iocommon_chown(pd, owner, group);
    pvfs_close(pd->fd);
}

int pvfs_fchown (int fd, uid_t owner, gid_t group)
{
    pvfs_descriptor *pd;
    pd = pvfs_find_descriptor(fd);
    iocommon_chown(pd, owner, group);
}

int pvfs_fchownat(int fd, char *path, uid_t owner, gid_t group, int flag)
{
    pvfs_descriptor *pd, *pd2;
    if (path[0] == '/' || fd == AT_FDCWD)
        if (flag & AT_SYMLINK_NOFOLLOW)
            pvfs_lchown(path, owner, group);
        else
            pvfs_chown(path, owner, group);
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
            flags |= O_NOFOLLOW;
        pd = pvfs_find_descriptor(fd);
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        iocommon_chown(pd2, owner, group);
        pvfs_close(pd2->fd);
    }
}

int pvfs_lchown (const char *path, uid_t owner, gid_t group)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    iocommon_chown(pd, owner, group);
    pvfs_close(pd->fd);
}

int pvfs_chmod (const char *path, mode_t mode)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    iocommon_chmod(pd, mode);
    pvfs_close(pd->fd);
}

int pvfs_fchmod (int fd, mode_t mode)
{
    pvfs_descriptor *pd;
    pd = pvfs_find_descriptor(fd);
    iocommon_chmod(pd, mode);
}

int pvfs_fchmodat(int fd, char *path, mode_t mode, int flag)
{
    pvfs_descriptor *pd, *pd2;
    if (path[0] == '/' || fd == AT_FDCWD)
        if (flag & AT_SYMLINK_NOFOLLOW)
            pvfs_lchmod(path, mode);
        else
            pvfs_chmod(path, mode);
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
            flags |= O_NOFOLLOW;
        pd = pvfs_find_descriptor(fd);
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        iocommon_chmod(pd2, mode);
        pvfs_close(pd2->fd);
    }
}

/* this is not a Linux syscall, but its useful above */
int pvfs_lchmod (const char *path, mode_t mode)
{
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    iocommon_chmod(pd, mode);
    pvfs_close(pd->fd);
}

int pvfs_mkdir (const char *path, mode_t mode)
{
    char *newpath;
    int rc;

    newpath = pvfs_qualify_path(path);
    rc = iocommon_make_directory(newpath, (mode & ~mask_val & 0777));
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

int pvfs_mkdirat (int dirfd, const char *path, mode_t mode)
{
    pvfs_descriptor *pd, *pd2;
    if (path[0] == '/' || dirfd == AT_FDCWD)
        pvfs_mkdir(path, mode);
    else
    {
        int flags = O_RDONLY;
        pd = pvfs_find_descriptor(dirfd);
        /* not sure what to do with this ... */
        return -1;
    }
}

int pvfs_rmdir (const char *path)
{
    char *newpath;
    int rc;

    newpath = pvfs_qualify_path(path);
    rc = iocommon_rmdir(newpath);
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

ssize_t pvfs_readlink (const char *path, char *buf, size_t bufsiz)
{
}

int pvfs_readlinkat (int dirfd, const char *path, char *buf, size_t bufsiz)
{
}

ssize_t pvfs_symlink (const char *oldpath, const char *newpath)
{
}

int pvfs_symlinkat (const char *oldpath, int newdirfd, const char *newpath)
{
}

/* PVFS does not have hard links */
ssize_t pvfs_link (const char *oldpath, const char *newpath)
{
}

/* PVFS does not have hard links */
int pvfs_linkat (const char *oldpath, int newdirfd,
                 const char *newpath, int flags)
{
}

/* this reads exactly one dirent, count is ignored */
int pvfs_readdir(unsigned int fd, struct dirent *dirp, unsigned int count)
{

    return 1; /* success */
    return 0; /* end of file */
    return -1; /* error */
}

/* this reads multiple dirents, up to count */
int pvfs_getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    int bytes;
    return bytes; /* success */
    return 0; /* end of file */
    return -1; /* error */
}

int pvfs_access (const char * path, int mode)
{
}

int pvfs_faccessat (int dirfd, const char * path, int mode, int flags)
{
}

int pvfs_flock(int fd, int op)
{
}

int pvfs_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    long arg;
    struct flock *lock;
}

/* sync all disk data */
void pvfs_sync(void )
{
}

/* sync file, but not dir it is in */
int pvfs_fsync(int fd)
{
    return 0; /* success */
}

/* does not sync file metadata */
int pvfs_fdatasync(int fd)
{
    return 0; /* success */
}

mode_t pvfs_umask(mode_t mask)
{
    mode_t old_mask = mask_val;
    mask_val = mask & 0777;
    return old_mask;
}

mode_t pvfs_getumask(void)
{
    return mask_val;
}

int pvfs_getdtablesize(void)
{
    return pvfs_descriptor_table_size();
}

posix_ops pvfs_ops = 
{
    .statfs = statfs,
    .open = pvfs_open,
    .open64 = pvfs_open64,
    .openat = pvfs_openat,
    .openat64 = pvfs_openat64,
    .creat = pvfs_creat,
    .creat64 = pvfs_creat64,
    .unlink = pvfs_unlink,
    .unlinkat = pvfs_unlinkat,
    .rename = pvfs_rename,
    .renameat = pvfs_renameat,
    .read = pvfs_read,
    .pread = pvfs_pread,
    .readv = pvfs_readv,
    .pread64 = pvfs_pread64,
    .write = pvfs_write,
    .pwrite = pvfs_pwrite,
    .writev = pvfs_writev,
    .write64 = pvfs_write64,
    .lseek = pvfs_lseek,
    .lseek64 = pvfs_lseek64,
    .truncate = pvfs_truncate,
    .truncate64 = pvfs_truncate64,
    .ftruncate = pvfs_ftruncate,
    .ftruncate64 = pvfs_ftruncate64,
    .close = pvfs_close,
    .flush = pvfs_flush,
    .stat = pvfs_stat,
    .stat64 = pvfs_stat64,
    .fstat = pvfs_fstat,
    .fstat64 = pvfs_fstat64,
    .fstatat = pvfs_fstatat,
    .fstatat64 = pvfs_fstatat64,
    .lstat = pvfs_lstat,
    .lstat64 = pvfs_lstat64,
    .dup = pvfs_dup,
    .dup2 = pvfs_dup2,
    .chown = pvfs_chown,
    .fchown = pvfs_fchown,
    .fchownat = pvfs_fchownat,
    .lchown = pvfs_lchown,
    .chmod = pvfs_chmod,
    .fchmod = pvfs_fchmod,
    .fchmodat = pvfs_fchmodat,
    .lchmod = pvfs_lchmod,
    .mkdir = pvfs_mkdir,
    .mkdirat = pvfs_mkdirat,
    .rmdir = pvfs_rmdir,
    .readlink = pvfs_readlink,
    .readlinkat = pvfs_readlinkat,
    .symlink = pvfs_symlink,
    .symlinkat = pvfs_symlinkat,
    .link = pvfs_link,
    .linkat = pvfs_linkat,
    .readdir = pvfs_readdir,
    .getdents = pvfs_getdents,
    .access = pvfs_access,
    .faccessat = pvfs_faccessat,
    .flock = pvfs_flock,
    .fcntl = pvfs_fcntl,
    .sync = pvfs_sync,
    .fsync = pvfs_fsync,
    .fdatasync = pvfs_fdatasync,
    .umask = pvfs_umask,
    .getumask = pvfs_getumask,
    .getdtablesize = pvfs_getdtablesize
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

