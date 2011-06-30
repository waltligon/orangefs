/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - pvfs version of posix system calls
 */
#include <usrint.h>
#include <linux/dirent.h>
#include <posix-ops.h>
#include <posix-pvfs.h>
#include <openfile-util.h>
#include <iocommon.h>

static mode_t mask_val = 0022; /* implements umask for pvfs library */

/* actual implementation of read and write are in these static funcs */

static ssize_t pvfs_prdwr64(int fd,
                            void *buf,
                            size_t count,
                            off64_t offset,
                            int which);

static ssize_t pvfs_rdwrv(int fd,
                          const struct iovec *vector,
                          size_t count,
                          int which);

/**
 *  pvfs_open
 */
int pvfs_open(const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    char *newpath;
    pvfs_descriptor *pd;

    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
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
    if (!pd)
    {
        return -1;
    }
    return pd->fd;
}

/**
 * pvfs_open64
 */
int pvfs_open64(const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;

    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    va_start(ap, flags);
    if (flags & O_CREAT)
    {
        mode = va_arg(ap, int);
    }
    else
    {
        mode = 0777;
    }
    if (flags & O_HINTS)
    {
        hints = va_arg(ap, PVFS_hint);
    }
    else
    {
        hints = PVFS_HINT_NULL;
    }
    va_end(ap);
    flags |= O_LARGEFILE;
    return pvfs_open(path, flags, mode);
}

/**
 * pvfs_openat
 */
int pvfs_openat(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    pvfs_descriptor *dpd, *fpd;

    if (!path)
    {
        errno - EINVAL;
        return -1;
    }
    va_start(ap, flags);
    if (flags & O_CREAT)
    {
        mode = va_arg(ap, int);
    }
    else
    {
        mode = 0777;
    }
    if (flags & O_HINTS)
    {
        hints = va_arg(ap, PVFS_hint);
    }
    else
    {
        hints = PVFS_HINT_NULL;
    }
    va_end(ap);
    if (path[0] == '/' || dirfd == AT_FDCWD)
    {
        return pvfs_open(path, flags, mode);
    }
    else
    {
        if (dirfd < 0)
        {
            errno = EBADF;
            return -1;
        }
        dpd = pvfs_find_descriptor(dirfd);
        if (!dpd)
        {
            return -1;
        }
        fpd = iocommon_open(path, flags, hints, mode, &dpd->pvfs_ref);
        if (!fpd)
        {
            return -1;
        }
        return fpd->fd;
    }
}

/**
 * pvfs_openat64
 */
int pvfs_openat64(int dirfd, const char *path, int flags, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    pvfs_descriptor *pd;

    if (dirfd < 0)
    {
        errno = EBADF;
        return -1;
    }
    va_start(ap, flags);
    if (flags & O_CREAT)
    {
        mode = va_arg(ap, int);
    }
    else
    {
        mode = 0777;
    }
    if (flags & O_HINTS)
    {
        hints = va_arg(ap, PVFS_hint);
    }
    else
    {
        hints = PVFS_HINT_NULL;
    }
    va_end(ap);
    flags |= O_LARGEFILE;
    return pvfs_openat(dirfd, path, flags, mode);
}

/**
 * pvfs_creat wrapper
 */
int pvfs_creat(const char *path, mode_t mode, ...)
{
    return pvfs_open(path, O_RDWR | O_CREAT | O_EXCL, mode);
}

/**
 * pvfs_creat64 wrapper
 */
int pvfs_creat64(const char *path, mode_t mode, ...)
{
    return pvfs_open64(path, O_RDWR | O_CREAT | O_EXCL, mode);
}

/**
 * pvfs_unlink
 */
int pvfs_unlink (const char *path)
{
    return iocommon_unlink(path, NULL);
}

/**
 * pvfs_unlinkat
 */
int pvfs_unlinkat (int dirfd, const char *path)
{
    int rc;
    pvfs_descriptor *pd;

    if (path[0] == '/' || dirfd == AT_FDCWD)
    {
        rc = iocommon_unlink(path, NULL);
    }
    else
    {
        int flags = O_RDONLY;
        if (dirfd < 0)
        {
            errno = EBADF;
            return -1;
        }       
        pd = pvfs_find_descriptor(dirfd);
        if (!pd)
        {
            errno = EBADF;
            return -1;
        }
        rc = iocommon_unlink(path, &pd->pvfs_ref);
    }
    return rc;
}

/**
 * pvfs_rename
 */
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

/**
 * pvfs_renameat
 */
int pvfs_renameat(int olddirfd, const char *oldpath,
                  int newdirfd, const char *newpath)
{
    int rc;
    pvfs_descriptor *olddes, *newdes;
    char *absoldpath, *absnewpath;

    if (!oldpath || !newpath)
    {
        errno = EINVAL;
        return -1;
    }
    if (oldpath[0] != '/')
    {
        if (olddirfd < 0)
        {
            errno = EBADF;
            return -1;
        }
        olddes = pvfs_find_descriptor(olddirfd);
        if (!olddes)
        {
            errno = EBADF;
            return -1;
        }
        absoldpath = (char *)oldpath;
    }
    else
    {
        olddes = NULL;
        absoldpath = pvfs_qualify_path(oldpath);
    }
    if (oldpath[0] != '/')
    {
        if (newdirfd < 0)
        {
            errno = EBADF;
            return -1;
        }
        newdes = pvfs_find_descriptor(newdirfd);
        if (!newdes)
        {
            errno = EBADF;
            return -1;
        }
        absnewpath = (char *)newpath;
    }
    else
    {
        newdes = NULL;
        absnewpath = pvfs_qualify_path(newpath);
    }
    rc = iocommon_rename(&olddes->pvfs_ref, absoldpath,
                         &newdes->pvfs_ref, absnewpath);
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

/**
 * pvfs_read wrapper
 */
ssize_t pvfs_read(int fd, void *buf, size_t count)
{
    int rc;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pvfs_descriptor *pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        return -1;
    }
    rc = pvfs_prdwr64(fd, buf, count, pd->file_pointer, PVFS_IO_READ);
    if (rc == -1)
    {
        return -1;
    }
    pd->file_pointer += rc;
    return rc;
}

/**
 * pvfs_pread wrapper
 */
ssize_t pvfs_pread(int fd, void *buf, size_t count, off_t offset)
{
    return pvfs_prdwr64(fd, buf, count, (off64_t) offset, PVFS_IO_READ);
}

/**
 * pvfs_readv wrapper
 */
ssize_t pvfs_readv(int fd, const struct iovec *vector, int count)
{
    return pvfs_rdwrv(fd, vector, count, PVFS_IO_READ);
}

/**
 * pvfs_pread64 wrapper
 */
ssize_t pvfs_pread64( int fd, void *buf, size_t count, off64_t offset )
{
    return pvfs_prdwr64(fd, buf, count, offset, PVFS_IO_READ);
}

/**
 * pvfs_write wrapper
 */
ssize_t pvfs_write(int fd, const void *buf, size_t count)
{
    int rc;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pvfs_descriptor *pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        return -1;
    }
    rc = pvfs_prdwr64(fd, (void *)buf, count, pd->file_pointer, PVFS_IO_WRITE);
    if (rc == -1)
    {
        return -1;
    }
    pd->file_pointer += rc;
    return rc;
}

/**
 * pvfs_pwrite wrapper
 */
ssize_t pvfs_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    return pvfs_prdwr64(fd, (void *)buf, count, (off64_t)offset, PVFS_IO_WRITE);
}

/**
 * pvfs_writev wrapper
 */
ssize_t pvfs_writev(int fd, const struct iovec *vector, int count)
{
    return pvfs_rdwrv(fd, vector, count, PVFS_IO_WRITE);
}

/**
 * pvfs_pwrite64 wrapper
 */
ssize_t pvfs_pwrite64(int fd, const void *buf, size_t count, off64_t offset)
{
    return pvfs_prdwr64(fd, (void *)buf, count, offset, PVFS_IO_WRITE);
}

/**
 * implements pread and pwrite with 64-bit file pointers
 */
static ssize_t pvfs_prdwr64(int fd,
                            void *buf,
                            size_t count,
                            off64_t offset,
                            int which)
{
    int rc;
    pvfs_descriptor* pd;
    PVFS_Request freq, mreq;

    memset(&freq, 0, sizeof(freq));
    memset(&mreq, 0, sizeof(mreq));

    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }

    rc = PVFS_Request_contiguous(count, PVFS_BYTE, &freq);
    rc = PVFS_Request_contiguous(count, PVFS_BYTE, &mreq);

    rc = iocommon_readorwrite(which, pd, offset, buf, mreq, freq);

    PVFS_Request_free(&freq);
    PVFS_Request_free(&mreq);

    return rc;
}

/**
 * implements readv and writev
 */
static ssize_t pvfs_rdwrv(int fd,
                          const struct iovec *vector,
                          size_t count,
                          int which)
{
    int rc;
    pvfs_descriptor* pd;
    PVFS_Request freq, mreq;
    off64_t offset;
    void *buf;

    memset(&freq, 0, sizeof(freq));
    memset(&mreq, 0, sizeof(mreq));

    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if(!pd)
    {
        return -1;
    }
    offset = pd->file_pointer;

    rc = PVFS_Request_contiguous(count, PVFS_BYTE, &freq);
    rc = pvfs_convert_iovec(vector, count, &mreq, &buf);

    rc = iocommon_readorwrite(which, pd, offset, buf, mreq, freq);

    if(rc != -1)
    {
        pd->file_pointer += rc;
    }

    PVFS_Request_free(&freq);
    PVFS_Request_free(&mreq);

    return rc;
}

/**
 * pvfs_lseek wrapper
 */
off_t pvfs_lseek(int fd, off_t offset, int whence)
{
    return (off_t) pvfs_lseek64(fd, (off64_t)offset, whence);
}

/**
 * pvfs_lseek64
 */
off64_t pvfs_lseek64(int fd, off64_t offset, int whence)
{
    pvfs_descriptor* pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }

    iocommon_lseek(pd, offset, 1, whence);

    return pd->file_pointer;
}

/**
 * pvfs_truncate wrapper
 */
int pvfs_truncate(const char *path, off_t length)
{
    return pvfs_truncate64(path, (off64_t) length);
}

/**
 * pvfs_truncate64
 */
int pvfs_truncate64 (const char *path, off64_t length)
{
    int rc;
    pvfs_descriptor *pd;

    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    pd = iocommon_open(path, O_WRONLY, PVFS_HINT_NULL, 0 , NULL);
    if (!pd)
    {
        return -1;
    }
    rc = iocommon_truncate(pd->pvfs_ref, length);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_ftruncate wrapper
 */
int pvfs_ftruncate (int fd, off_t length)
{
    return pvfs_ftruncate64(fd, (off64_t) length);
}

/**
 * pvfs_ftruncate64
 */
int pvfs_ftruncate64 (int fd, off64_t length)
{
    pvfs_descriptor *pd;
    
    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        return -1;
    }
    return iocommon_truncate(pd->pvfs_ref, length);
}

/**
 * pvfs_close
 *
 * TODO: add open/close count to minimize metadata ops
 * this may only work if we have multi-user caching
 * which we don't for now
 */
int pvfs_close(int fd)
{
    pvfs_descriptor* pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    /* flush buffers */
    pvfs_flush(fd);

    /* free descriptor */
    pvfs_free_descriptor(fd);

    return PVFS_FD_SUCCESS;
}

/**
 * pvfs_flush
 */
int pvfs_flush(int fd)
{
    pvfs_descriptor* pd;

#ifdef DEBUG
    pvfs_debug("in pvfs_flush(%ld)\n", fd);
#endif

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return PVFS_FD_FAILURE;
    }

    /* tell the server to flush data to disk */
    return iocommon_fsync(pd);
}

/* various flavors of stat */
/**
 * pvfs_stat
 */
int pvfs_stat(const char *path, struct stat *buf)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    rc = iocommon_stat(pd, buf);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_stat64
 */
int pvfs_stat64(const char *path, struct stat64 *buf)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    rc = iocommon_stat64(pd, buf);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_fstat
 */
int pvfs_fstat(int fd, struct stat *buf)
{
    pvfs_descriptor *pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_stat(pd, buf);
}

/**
 * pvfs_fstat64
 */
int pvfs_fstat64(int fd, struct stat64 *buf)
{
    pvfs_descriptor *pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_stat64(pd, buf);
}

/**
 * pvfs_fstatat
 */
int pvfs_fstatat(int fd, char *path, struct stat *buf, int flag)
{
    int rc;
    pvfs_descriptor *pd, *pd2;

    if (path[0] == '/' || fd == AT_FDCWD)
    {
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            rc = pvfs_lstat(path, buf);
        }
        else
        {
            rc = pvfs_stat(path, buf);
        }
    }
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            flags |= O_NOFOLLOW;
        }
        if (fd < 0)
        {
            errno = EBADF;
            return -1;
        }
        pd = pvfs_find_descriptor(fd);
        if (!pd)
        {
            return -1;
        }
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        rc = iocommon_stat(pd2, buf);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_fstatat64
 */
int pvfs_fstatat64(int fd, char *path, struct stat64 *buf, int flag)
{
    int rc;
    pvfs_descriptor *pd, *pd2;

    if (path[0] == '/' || fd == AT_FDCWD)
    {
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            rc = pvfs_lstat64(path, buf);
        }
        else
        {
            rc = pvfs_stat64(path, buf);
        }
    }
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            flags |= O_NOFOLLOW;
        }
        if (fd < 0)
        {
            errno = EBADF;
            return -1;
        }
        pd = pvfs_find_descriptor(fd);
        if (!pd)
        {
            errno = EBADF;
            return -1;
        }
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        rc = iocommon_stat64(pd2, buf);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_lstat
 */
int pvfs_lstat(const char *path, struct stat *buf)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
       free(newpath);
    }
    rc = iocommon_stat(pd, buf);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_lstat64
 */
int pvfs_lstat64(const char *path, struct stat64 *buf)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    rc = iocommon_stat64(pd, buf);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_dup
 */
int pvfs_dup(int oldfd)
{
    return pvfs_dup_descriptor(oldfd, -1);
}

/**
 * pvfs_dup2
 */
int pvfs_dup2(int oldfd, int newfd)
{
    return pvfs_dup_descriptor(oldfd, newfd);
}

/**
 * pvfs_chown
 */
int pvfs_chown (const char *path, uid_t owner, gid_t group)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    rc = iocommon_chown(pd, owner, group);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_fchown
 */
int pvfs_fchown (int fd, uid_t owner, gid_t group)
{
    pvfs_descriptor *pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_chown(pd, owner, group);
}

/**
 * pvfs_fchownat
 */
int pvfs_fchownat(int fd, char *path, uid_t owner, gid_t group, int flag)
{
    int rc;
    pvfs_descriptor *pd, *pd2;

    if (path[0] == '/' || fd == AT_FDCWD)
    {
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            rc = pvfs_lchown(path, owner, group);
        }
        else
        {
            rc = pvfs_chown(path, owner, group);
        }
    }
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            flags |= O_NOFOLLOW;
        }
        if (fd < 0)
        {
            errno = EBADF;
            return -1;
        }
        pd = pvfs_find_descriptor(fd);
        if (!pd)
        {
            return -1;
        }
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        rc = iocommon_chown(pd2, owner, group);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_lchown
 */
int pvfs_lchown (const char *path, uid_t owner, gid_t group)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    rc = iocommon_chown(pd, owner, group);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_chmod
 */
int pvfs_chmod (const char *path, mode_t mode)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    rc = iocommon_chmod(pd, mode);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_fchmod
 */
int pvfs_fchmod (int fd, mode_t mode)
{
    pvfs_descriptor *pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_chmod(pd, mode);
}

/**
 * pvfs_fchmodat
 */
int pvfs_fchmodat(int fd, char *path, mode_t mode, int flag)
{
    int rc;
    pvfs_descriptor *pd, *pd2;

    if (path[0] == '/' || fd == AT_FDCWD)
    {
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            rc = pvfs_lchmod(path, mode);
        }
        else
        {
            rc = pvfs_chmod(path, mode);
        }
    }
    else
    {
        int flags = O_RDONLY;
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            flags |= O_NOFOLLOW;
        }
        if (fd < 0)
        {
            errno = EBADF;
            return -1;
        }
        pd = pvfs_find_descriptor(fd);
        if (!pd)
        {
            return -1;
        }
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        rc = iocommon_chmod(pd2, mode);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_fchmodat
 *
 * this is not a Linux syscall, but its useful above 
 */
int pvfs_lchmod (const char *path, mode_t mode)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    rc = iocommon_chmod(pd, mode);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_mkdir
 */
int pvfs_mkdir (const char *path, mode_t mode)
{
    int rc;
    char *newpath;

    newpath = pvfs_qualify_path(path);
    rc = iocommon_make_directory(newpath, (mode & ~mask_val & 0777), NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * pvfs_mkdirat
 */
int pvfs_mkdirat (int dirfd, const char *path, mode_t mode)
{
    int rc;
    pvfs_descriptor *pd, *pd2;

    if (path[0] == '/' || dirfd == AT_FDCWD)
    {
        rc = pvfs_mkdir(path, mode);
    }
    else
    {
        if (dirfd < 0)
        {
            errno = EBADF;
            return -1;
        }
        pd = pvfs_find_descriptor(dirfd);
        if (!pd)
        {
            errno = EBADF;
            return -1;
        }
        rc = iocommon_make_directory(path,
                                     (mode & ~mask_val & 0777),
                                     &pd->pvfs_ref);
    }
    return rc;
}

/**
 * pvfs_rmdir
 */
int pvfs_rmdir (const char *path)
{
    int rc;
    char *newpath;

    newpath = pvfs_qualify_path(path);
    rc = iocommon_rmdir(newpath, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * readlink fills buffer with contents of a symbolic link
 *
 */
ssize_t pvfs_readlink (const char *path, char *buf, size_t bufsiz)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    pd = iocommon_open(newpath, O_RDONLY | O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (newpath != path)
    {
       free(newpath);
    }
    /* this checks that it is a valid symlink and sets errno if not */
    rc = iocommon_readlink(pd, buf, bufsiz);
    pvfs_close(pd->fd);
    return rc;
}

int pvfs_readlinkat (int fd, const char *path, char *buf, size_t bufsiz)
{
    int rc;
    pvfs_descriptor *pd, *pd2;

    if (path[0] == '/' || fd == AT_FDCWD)
    {
        rc = pvfs_readlink(path, buf, bufsiz);
    }
    else
    {
        int flags = O_RDONLY | O_NOFOLLOW;
        if (fd < 0)
        {
            errno = EBADF;
            return -1;
        }
        pd = pvfs_find_descriptor(fd);
        if (!pd)
        {
            return -1;
        }
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, &pd->pvfs_ref);
        if(!pd2)
        {
            return -1;
        }
        rc = iocommon_readlink(pd2, buf, bufsiz);
        pvfs_close(pd2->fd);
    }
    return rc;
}

int pvfs_symlink (const char *oldpath, const char *newpath)
{
    return iocommon_symlink(newpath, oldpath, NULL);
}

int pvfs_symlinkat (const char *oldpath, int newdirfd, const char *newpath)
{
    pvfs_descriptor *pd;
    if (newdirfd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(newdirfd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_symlink(newpath, oldpath, &pd->pvfs_ref);
}

/**
 * PVFS does not have hard links
 */
ssize_t pvfs_link (const char *oldpath, const char *newpath)
{
}

/**
 * PVFS does not have hard links
 */
int pvfs_linkat (const char *oldpath, int newdirfd,
                 const char *newpath, int flags)
{
}

/**
 * this reads exactly one dirent, count is ignored
 */
int pvfs_readdir(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    return pvfs_getdents(fd, dirp, 1);
}

/**
 * this reads multiple dirents, up to count
 */
int pvfs_getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    pvfs_descriptor *pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_getdents(pd, dirp, count);
}

int pvfs_access (const char * path, int mode)
{
    return iocommon_access(path, mode, 0, NULL);
}

int pvfs_faccessat (int fd, const char * path, int mode, int flags)
{
    pvfs_descriptor *pd;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if(!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_access(path, mode, flags, &pd->pvfs_ref);
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

/**
 * pvfs_umask
 */
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
/*  .write64 = pvfs_write64, */
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

