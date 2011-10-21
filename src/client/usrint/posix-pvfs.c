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
#define USRINT_SOURCE 1
#include "usrint.h"
#include "posix-ops.h"
#include "posix-pvfs.h"
#include "openfile-util.h"
#include "iocommon.h"

#define PVFS_ATTR_DEFAULT_MASK \
(PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_SIZE | PVFS_ATTR_SYS_BLKSIZE)

static mode_t mask_val = 0022; /* implements umask for pvfs library */
static char pvfs_cwd[PVFS_PATH_MAX];

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
    debug("pvfs_open: called with %s\n", path);

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
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, flags, hints, mode, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    if (!pd)
    {
        return -1;
    }
    else
    {
        return pd->fd;
    }
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
        fpd = iocommon_open(path, flags, hints, mode, dpd);
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
int pvfs_unlink(const char *path)
{
    int rc = 0;
    char *newpath;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    rc = iocommon_unlink(path, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * pvfs_unlinkat
 */
int pvfs_unlinkat(int dirfd, const char *path, int flags)
{
    int rc;
    pvfs_descriptor *pd;

    if (path[0] == '/' || dirfd == AT_FDCWD)
    {
        rc = iocommon_unlink(path, NULL);
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
        if (flags & AT_REMOVEDIR)
        {
            rc = iocommon_rmdir(path, &pd->s->pvfs_ref);
        }
        else
        {
            rc = iocommon_unlink(path, &pd->s->pvfs_ref);
        }
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
    if (!absoldpath)
    {
        return -1;
    }
    absnewpath = pvfs_qualify_path(newpath);
    if (!absnewpath)
    {
        free(absoldpath);
        return -1;
    }
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
    pvfs_descriptor *pd;
    PVFS_object_ref *olddirref, *newdirref;
    char *absoldpath, *absnewpath;

    if (!oldpath || !newpath)
    {
        errno = EINVAL;
        return -1;
    }
    if (oldpath[0] == '/' || olddirfd == AT_FDCWD)
    {
        olddirref = NULL;
        absoldpath = pvfs_qualify_path(oldpath);
        if (!absoldpath)
        {
            return -1;
        }
    }
    else
    {
        if (olddirfd < 0)
        {
            errno = EBADF;
            return -1;
        }
        pd = pvfs_find_descriptor(olddirfd);
        if (!pd)
        {
            errno = EBADF;
            return -1;
        }
        olddirref = &pd->s->pvfs_ref;
        absoldpath = (char *)oldpath;
    }
    if (oldpath[0] == '/' || newdirfd == AT_FDCWD)
    {
        newdirref = NULL;
        absnewpath = pvfs_qualify_path(newpath);
        if (!absnewpath)
        {
            return -1;
        }
    }
    else
    {
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
        newdirref = &pd->s->pvfs_ref;
        absnewpath = (char *)newpath;
    }
    rc = iocommon_rename(olddirref, absoldpath, newdirref, absnewpath);
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
    rc = pvfs_prdwr64(fd, buf, count, pd->s->file_pointer, PVFS_IO_READ);
    if (rc < 0)
    {
        return -1;
    }
    pd->s->file_pointer += rc;
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
    off64_t offset;

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
    /* check for append mode */
    if (pd->s->flags & O_APPEND)
    {
        struct stat sbuf;
        pvfs_fstat(fd, &sbuf);
        offset = sbuf.st_size;
    }
    else
    {
        offset = pd->s->file_pointer;
    }
    rc = pvfs_prdwr64(fd, (void *)buf, count, offset, PVFS_IO_WRITE);
    if (rc < 0)
    {
        return -1;
    }
    pd->s->file_pointer += rc;
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
                            size_t size,
                            off64_t offset,
                            int which)
{
    int rc;
    pvfs_descriptor* pd;
    PVFS_Request freq, mreq;
    struct iovec vector[1];

    memset(&freq, 0, sizeof(freq));
    memset(&mreq, 0, sizeof(mreq));

    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }

    rc = PVFS_Request_contiguous(size, PVFS_BYTE, &freq);
    rc = PVFS_Request_contiguous(size, PVFS_BYTE, &mreq);

    /* place contiguous buff and count into an iovec array of length 1 */
    vector[0].iov_base = buf;
    vector[0].iov_len = size;

    rc = iocommon_readorwrite(which, pd, offset, buf, mreq, freq, 1, vector);

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
    int rc = 0;
    int i, size = 0;
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
    offset = pd->s->file_pointer;

    for (i = 0; i < count; i++)
    {
        size += vector[i].iov_len;
    }

    rc = PVFS_Request_contiguous(size, PVFS_BYTE, &freq);
    rc = pvfs_convert_iovec(vector, count, &mreq, &buf);

    rc = iocommon_readorwrite(which, pd, offset, buf, mreq, freq, count, vector);

    if (rc >= 0)
    {
        pd->s->file_pointer += rc;
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

    return pd->s->file_pointer;
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
int pvfs_truncate64(const char *path, off64_t length)
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
    rc = iocommon_truncate(pd->s->pvfs_ref, length);
    pvfs_close(pd->fd);
    return rc;
}

/**
 * pvfs_allocate wrapper
 *
 * This isn't right but we dont' have a syscall to match this.
 * Best effort is to tuncate to thex size, which should guarantee
 * spaceis available starting at beginning (let alone offset)
 * extending to offset+length.
 *
 * Our truncate doesn't always allocate blocks either, since
 * the underlying FS may have a sparse implementation.
 */
int pvfs_fallocate(int fd, off_t offset, off_t length)
{
    if (offset < 0 || length < 0)
    {
        errno = EINVAL;
        return -1;
    }
    /* if (file_size < offset + length)
     * {
     */
    return pvfs_ftruncate64(fd, (off64_t)(offset) + (off64_t)(length));
}

/**
 * pvfs_ftruncate wrapper
 */
int pvfs_ftruncate(int fd, off_t length)
{
    return pvfs_ftruncate64(fd, (off64_t) length);
}

/**
 * pvfs_ftruncate64
 */
int pvfs_ftruncate64(int fd, off64_t length)
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
    return iocommon_truncate(pd->s->pvfs_ref, length);
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
    int rc = 0;
    pvfs_descriptor* pd;
    debug("pvfs_close: called with %d\n", fd);

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
    if (S_ISREG(pd->s->mode))
    {
        rc = iocommon_fsync(pd);
        if (rc < 0)
        {
            return -1;
        }
    }

    /* free descriptor */
    rc = pvfs_free_descriptor(pd->fd);
    if (rc < 0)
    {
        return -1;
    }

    debug("pvfs_close: returns %d\n", rc);
    return rc;
}

/* various flavors of stat */
/**
 * pvfs_stat
 */
int pvfs_stat(const char *path, struct stat *buf)
{
    return pvfs_stat_mask(path, buf, PVFS_ATTR_DEFAULT_MASK);
}

int pvfs_stat_mask(const char *path, struct stat *buf, uint32_t mask)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    mask &= PVFS_ATTR_DEFAULT_MASK;
    rc = iocommon_stat(pd, buf, mask);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
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
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_stat64(pd, buf, PVFS_ATTR_DEFAULT_MASK);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * pvfs_fstat
 */
int pvfs_fstat(int fd, struct stat *buf)
{
    return pvfs_fstat_mask(fd, buf, PVFS_ATTR_DEFAULT_MASK);
}

int pvfs_fstat_mask(int fd, struct stat *buf, uint32_t mask)
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
    mask &= PVFS_ATTR_DEFAULT_MASK;
    return iocommon_stat(pd, buf, mask);
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
    return iocommon_stat64(pd, buf, PVFS_ATTR_DEFAULT_MASK);
}

/**
 * pvfs_fstatat
 */
int pvfs_fstatat(int fd, const char *path, struct stat *buf, int flag)
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
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, pd);
        if (!pd2)
        {
            return -1;
        }
        rc = iocommon_stat(pd2, buf, PVFS_ATTR_DEFAULT_MASK);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_fstatat64
 */
int pvfs_fstatat64(int fd, const char *path, struct stat64 *buf, int flag)
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
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, pd);
        if (!pd2)
        {
            return -1;
        }
        rc = iocommon_stat64(pd2, buf, PVFS_ATTR_DEFAULT_MASK);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_lstat
 */
int pvfs_lstat(const char *path, struct stat *buf)
{
    return pvfs_lstat_mask(path, buf, PVFS_ATTR_DEFAULT_MASK);
}

int pvfs_lstat_mask(const char *path, struct stat *buf, uint32_t mask)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    mask &= PVFS_ATTR_DEFAULT_MASK;
    rc = iocommon_stat(pd, buf, mask);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
       free(newpath);
    }
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
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_stat64(pd, buf, PVFS_ATTR_DEFAULT_MASK);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * pvfs_futimesat
 */
int pvfs_futimesat(int dirfd,
                   const char *path,
                   const struct timeval times[2])
{
    int rc = 0;
    pvfs_descriptor *pd=NULL, *pd2=NULL;
    PVFS_sys_attr attr;

    if (dirfd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(dirfd);
    if (!pd)
    {
        return -1;
    }
    if (path)
    {
        pd2 = iocommon_open(path, O_RDONLY, PVFS_HINT_NULL, 0, pd);
    }
    else
    {
        pd2 = pd; /* allow null path to work */
    }
    if (!pd2)
    {
        return -1;
    }
    memset(&attr, 0, sizeof(attr));
    if (!times)
    {
        struct timeval curtime;
        gettimeofday(&curtime, NULL);
        attr.atime = curtime.tv_sec;
        attr.mtime = curtime.tv_sec;
    }
    else
    {
        attr.atime = times[0].tv_sec;
        attr.mtime = times[1].tv_sec;
    }
    attr.mask = PVFS_ATTR_SYS_ATIME | PVFS_ATTR_SYS_MTIME;
    rc = iocommon_setattr(pd2->s->pvfs_ref, &attr);
    if (path)
    {
        pvfs_close(pd2->fd);
    }
    return rc;
}

int pvfs_utimes(const char *path, const struct timeval times[2])
{
    return pvfs_futimesat(AT_FDCWD, path, times);
}

int pvfs_utime(const char *path, const struct utimbuf *buf)
{
    struct timeval times[2];
    times[0].tv_sec = buf->actime;
    times[0].tv_usec = 0;
    times[1].tv_sec = buf->modtime;
    times[1].tv_usec = 0;
    return pvfs_futimesat(AT_FDCWD, path, times);
}

int pvfs_futimes(int fd, const struct timeval times[2])
{
    int rc = 0;
    pvfs_descriptor *pd=NULL;
    PVFS_sys_attr attr;

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
    memset(&attr, 0, sizeof(attr));
    if (!times)
    {
        struct timeval curtime;
        gettimeofday(&curtime, NULL);
        attr.atime = curtime.tv_sec;
        attr.mtime = curtime.tv_sec;
    }
    else
    {
        attr.atime = times[0].tv_sec;
        attr.mtime = times[1].tv_sec;
    }
    attr.mask = PVFS_ATTR_SYS_ATIME | PVFS_ATTR_SYS_MTIME;
    rc = iocommon_setattr(pd->s->pvfs_ref, &attr);
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
int pvfs_chown(const char *path, uid_t owner, gid_t group)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_chown(pd, owner, group);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * pvfs_fchown
 */
int pvfs_fchown(int fd, uid_t owner, gid_t group)
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
int pvfs_fchownat(int fd, const char *path, uid_t owner, gid_t group, int flag)
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
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, pd);
        if (!pd)
        {
            return -1;
        }
        rc = iocommon_chown(pd2, owner, group);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_lchown
 */
int pvfs_lchown(const char *path, uid_t owner, gid_t group)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY|O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_chown(pd, owner, group);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * pvfs_chmod
 */
int pvfs_chmod(const char *path, mode_t mode)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_chmod(pd, mode);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

/**
 * pvfs_fchmod
 */
int pvfs_fchmod(int fd, mode_t mode)
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
int pvfs_fchmodat(int fd, const char *path, mode_t mode, int flag)
{
    int rc;
    pvfs_descriptor *pd, *pd2;

    if (path[0] == '/' || fd == AT_FDCWD)
    {
        rc = pvfs_chmod(path, mode);
    }
    else
    {
        int flags = O_RDONLY;
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
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, pd);
        if (!pd2)
        {
            return -1;
        }
        rc = iocommon_chmod(pd2, mode);
        pvfs_close(pd2->fd);
    }
    return rc;
}

/**
 * pvfs_mkdir
 */
int pvfs_mkdir(const char *path, mode_t mode)
{
    int rc;
    char *newpath;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
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
int pvfs_mkdirat(int dirfd, const char *path, mode_t mode)
{
    int rc;
    pvfs_descriptor *pd;

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
                                     &pd->s->pvfs_ref);
    }
    return rc;
}

/**
 * pvfs_rmdir
 */
int pvfs_rmdir(const char *path)
{
    int rc;
    char *newpath;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
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
ssize_t pvfs_readlink(const char *path, char *buf, size_t bufsiz)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY | O_NOFOLLOW, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    debug("pvfs_readlink mode is %o\n", pd->s->mode);
    /* this checks that it is a valid symlink and sets errno if not */
    rc = iocommon_readlink(pd, buf, bufsiz);
    /* need to close if readlink succeeds or not */
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
       free(newpath);
    }
    return rc;
}

ssize_t pvfs_readlinkat(int fd, const char *path, char *buf, size_t bufsiz)
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
        pd2 = iocommon_open(path, flags, PVFS_HINT_NULL, 0, pd);
        if(!pd2)
        {
            return -1;
        }
        rc = iocommon_readlink(pd2, buf, bufsiz);
        pvfs_close(pd2->fd);
    }
    return rc;
}

int pvfs_symlink(const char *oldpath, const char *newpath)
{
    int rc = 0;
    char *abspath;
    abspath = pvfs_qualify_path(newpath);
    if (!abspath)
    {
        return -1;
    }
    rc = iocommon_symlink(abspath, oldpath, NULL);
    if (abspath != newpath)
    {
       free(abspath);
    }
    return rc;
}

int pvfs_symlinkat(const char *oldpath, int newdirfd, const char *newpath)
{
    pvfs_descriptor *pd;

    if (newpath[0] == '/' || newdirfd == AT_FDCWD)
    {
        return pvfs_symlink(oldpath, newpath);
    }
    else
    {
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
    }
    return iocommon_symlink(newpath, oldpath, &pd->s->pvfs_ref);
}

/**
 * PVFS does not have hard links
 */
int pvfs_link(const char *oldpath, const char *newpath)
{
    fprintf(stderr, "pvfs_link not implemented\n");
    errno = ENOSYS;
    return -1;
}

/**
 * PVFS does not have hard links
 */
int pvfs_linkat(int olddirfd, const char *oldpath,
                int newdirfd, const char *newpath, int flags)
{
    fprintf(stderr, "pvfs_linkat not implemented\n");
    errno = ENOSYS;
    return -1;
}

/**
 * this reads exactly one dirent, count is ignored
 */
int pvfs_readdir(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    return pvfs_getdents(fd, dirp, 1);
}

/**
 * this reads multiple dirents, man pages calls last arg count but
 * is ambiguous if it is number of records or number of bytes.  latter
 * appears to be true so we renmame size.  Returns bytes read.
 */
int pvfs_getdents(unsigned int fd, struct dirent *dirp, unsigned int size)
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
    return iocommon_getdents(pd, dirp, size);
}

int pvfs_getdents64(unsigned int fd, struct dirent64 *dirp, unsigned int size)
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
    return iocommon_getdents64(pd, dirp, size);
}

int pvfs_access(const char *path, int mode)
{
    int rc = 0;
    char *newpath;
    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    rc = iocommon_access(path, mode, 0, NULL);
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

int pvfs_faccessat(int fd, const char *path, int mode, int flags)
{
    pvfs_descriptor *pd;

    if (path[0] == '/' || fd == AT_FDCWD)
    {
        return pvfs_access(path, mode);
    }
    else
    {
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
    }
    return iocommon_access(path, mode, flags, &pd->s->pvfs_ref);
}

int pvfs_flock(int fd, int op)
{
    errno = ENOSYS;
    fprintf(stderr, "pvfs_flock not implemented\n");
    return -1;
}

int pvfs_fcntl(int fd, int cmd, ...)
{
    int rc = 0;
    va_list ap;
    /* long arg; */
    struct flock *lock;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        rc = -1;
        goto errorout;
    }
    va_start(ap, cmd);
    switch (cmd)
    {
    case F_DUPFD :
    case F_GETFD :
        rc = pd->fdflags;
        break;
    case F_SETFD :
        pd->fdflags = va_arg(ap, int);
        break;
    case F_GETFL :
        rc = pd->s->flags;
        break;
    case F_SETFL :
        pd->s->flags = va_arg(ap, int);
        break;
    case F_GETLK :
    case F_SETLK :
    case F_SETLKW :
        lock = va_arg(ap, struct flock *);
    case F_GETOWN :
    case F_SETOWN :
    case F_GETSIG :
    case F_SETSIG :
    case F_GETLEASE :
    case F_SETLEASE :
    case F_NOTIFY :
    default :
        errno = ENOSYS;
        fprintf(stderr, "pvfs_fcntl command not implemented\n");
        rc = -1;
        break;
    }
    va_end(ap);

errorout :
    return rc;
}

/* sync all disk data */
void pvfs_sync(void )
{
    return;
}

/**
 * pvfs_fsync
 * sync file, but not dir it is in 
 * as close as we have for now 
 */
int pvfs_fsync(int fd)
{
    int rc = 0;
    pvfs_descriptor* pd;

    debug("pvfs_fsync: called with %d\n", fd);

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

    /* tell the server to flush data to disk */
    rc = iocommon_fsync(pd);
    debug("pvfs_fsync: returns %d\n", rc);
    return rc;
}

/* does not sync file metadata */
int pvfs_fdatasync(int fd)
{
    int rc = 0;

    rc = pvfs_fsync(fd); /* as close as we have for now */
    return rc;
}

int pvfs_fadvise(int fd, off_t offset, off_t len, int advice)
{
    return pvfs_fadvise64(fd, (off64_t) offset, (off64_t)len, advice);
}

/** fadvise implementation
 *
 * technically this is a hint, so doing nothing is still success
 */
int pvfs_fadvise64(int fd, off64_t offset, off64_t len, int advice)
{
    switch (advice)
    {
    case POSIX_FADV_NORMAL:
    case POSIX_FADV_RANDOM:
    case POSIX_FADV_SEQUENTIAL:
    case POSIX_FADV_WILLNEED:
    case POSIX_FADV_DONTNEED:
    case POSIX_FADV_NOREUSE:
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvfs_statfs(const char *path, struct statfs *buf)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_statfs(pd, buf);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

int pvfs_statfs64(const char *path, struct statfs64 *buf)
{
    int rc;
    char *newpath;
    pvfs_descriptor *pd;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_statfs64(pd, buf);
    pvfs_close(pd->fd);

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

int pvfs_fstatfs(int fd, struct statfs *buf)
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
    return iocommon_statfs(pd, buf);
}

int pvfs_fstatfs64(int fd, struct statfs64 *buf)
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
    return iocommon_statfs64(pd, buf);
}

int pvfs_statvfs(const char *path, struct statvfs *buf)
{
    int rc = 0;
    pvfs_descriptor *pd;
    struct statfs buf2;
    char *newpath;

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    pd = iocommon_open(newpath, O_RDONLY, PVFS_HINT_NULL, 0, NULL);
    if (!pd)
    {
        rc = -1;
        goto errorout;
    }
    rc = iocommon_statfs(pd, &buf2);
    pvfs_close(pd->fd);
    if (rc < 0)
    {
        goto errorout;
    }
    buf->f_bsize = buf2.f_bsize;
    /* buf->f_rsize */
    buf->f_blocks = buf2.f_blocks;
    buf->f_bfree = buf2.f_bfree;
    buf->f_bavail = buf2.f_bavail;
    buf->f_files = buf2.f_files;
    buf->f_ffree = buf2.f_ffree;
    /* buf->f_favail */
    buf->f_fsid = (unsigned long)buf2.f_fsid.__val[0];
    /* buf->f_flag */
    buf->f_namemax = buf2.f_namelen;

errorout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

int pvfs_fstatvfs(int fd, struct statvfs *buf)
{
    int rc = 0;
    pvfs_descriptor *pd;
    struct statfs buf2;

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
    rc = iocommon_statfs(pd, &buf2);
    if (rc < 0)
    {
        return -1;
    }
    buf->f_bsize = buf2.f_bsize;
    /* buf->f_rsize */
    buf->f_blocks = buf2.f_blocks;
    buf->f_bfree = buf2.f_bfree;
    buf->f_bavail = buf2.f_bavail;
    buf->f_files = buf2.f_files;
    buf->f_ffree = buf2.f_ffree;
    /* buf->f_favail */
    buf->f_fsid = (unsigned long)buf2.f_fsid.__val[0];
    /* buf->f_flag */
    buf->f_namemax = buf2.f_namelen;
    return rc;
}

int pvfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    return pvfs_mknodat(AT_FDCWD, path, mode, dev);
}

int pvfs_mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
    int fd;
    /* int s_type = mode & S_IFMT; */
    
    switch (dev)
    {
    case S_IFREG:
        fd = pvfs_openat(dirfd, path, O_CREAT|O_EXCL|O_RDONLY, mode & 0x777);
        if (fd < 0)
        {
            return -1;
        }
        pvfs_close(fd);
        break;
    case S_IFCHR:
    case S_IFBLK:
    case S_IFIFO:
    case S_IFSOCK:
    default:
        errno = EINVAL;
        return -1;
    }
    return 0;
}

ssize_t pvfs_sendfile(int outfd, int infd, off_t *offset, size_t count)
{
    return pvfs_sendfile64(outfd, infd, (off64_t *)offset, count);
}
                 
ssize_t pvfs_sendfile64(int outfd, int infd, off64_t *offset, size_t count)
{
    pvfs_descriptor *inpd, *outpd;

    inpd = pvfs_find_descriptor(infd);
    outpd = pvfs_find_descriptor(outfd);  /* this should be  a socket */
    if (!inpd || !outpd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_sendfile(outpd->true_fd, inpd, offset, count);
}

int pvfs_setxattr(const char *path,
                  const char *name,
                  const void *value,
                  size_t size,
                  int flags)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_fsetxattr(fd, name, value, size, flags);
    pvfs_close(fd);
    return rc;
}

int pvfs_lsetxattr(const char *path,
                   const char *name,
                   const void *value,
                   size_t size,
                   int flags)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR | O_NOFOLLOW);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_fsetxattr(fd, name, value, size, flags);
    pvfs_close(fd);
    return rc;
}

int pvfs_fsetxattr(int fd,
                   const char *name,
                   const void *value,
                   size_t size,
                   int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    rc = iocommon_seteattr(pd, name, value, size, flags);
    return rc;
}

int pvfs_getxattr(const char *path,
                  const char *name,
                  void *value,
                  size_t size)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_fgetxattr(fd, name, value, size);
    pvfs_close(fd);
    return rc;
}

int pvfs_lgetxattr(const char *path,
                   const char *name,
                   void *value,
                   size_t size)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR | O_NOFOLLOW);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_fgetxattr(fd, name, value, size);
    pvfs_close(fd);
    return rc;
}

int pvfs_fgetxattr(int fd,
                   const char *name,
                   void *value,
                   size_t size)
{
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_geteattr(pd, name, value, size);
}

int pvfs_listxattr(const char *path,
                   char *list,
                   size_t size)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_flistxattr(fd, list, size);
    pvfs_close(fd);
    return rc;
}

int pvfs_llistxattr(const char *path,
                    char *list,
                    size_t size)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR | O_NOFOLLOW);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_flistxattr(fd, list, size);
    pvfs_close(fd);
    return rc;
}

int pvfs_flistxattr(int fd,
                    char *list,
                    size_t size)
{
    int retsize, rc = 0;
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    rc = iocommon_listeattr(pd, list, size, &retsize);
    if (rc < 0)
    {
        return -1;
    }
    return retsize;
}

int pvfs_removexattr(const char *path,
                     const char *name)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_fremovexattr(fd, name);
    pvfs_close(fd);
    return rc;
}

int pvfs_lremovexattr(const char *path,
                      const char *name)
{
    int fd, rc = 0;

    fd = pvfs_open(path, O_RDWR | O_NOFOLLOW);
    if (fd < 0)
    {
        return fd;
    }
    rc = pvfs_fremovexattr(fd, name);
    pvfs_close(fd);
    return rc;
}

int pvfs_fremovexattr(int fd,
                      const char *name)
{
    pvfs_descriptor *pd;

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }
    return iocommon_deleattr(pd, name);
}

/* These functions simulate management of the current
 * working directory given than the kernel may not
 * be aware of PVFS virtual mounts
 */
int pvfs_cwd_init(const char *buf, size_t size)
{
    char *rv;
    memset(pvfs_cwd, 0, PVFS_PATH_MAX);
    rv = strncpy(pvfs_cwd, buf, PVFS_util_min(size, PVFS_PATH_MAX));
    if (!rv)
    {
        return -1;
    }
    return 0;
}

/**
 * pvfs chdir
 */
int pvfs_chdir(const char *path)
{
    int rc = 0, plen = 0;
    struct stat sbuf;
    char *newpath = NULL;

    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    /* we really need to resolve this to a cannonical path */
    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    /* basic path length check */
    plen = strlen(newpath);
    if (plen > PVFS_PATH_MAX)
    {
        errno = ENAMETOOLONG;
        rc = -1;
        goto errout;
    }
    /* if it is a valid path we can stat it and see what it is */
    rc = stat(newpath, &sbuf); /* this will get most errors */
    if (rc < 0)
    {
        rc = -1;
        goto errout;
    }
    /* path must be a directory */
    if (!S_ISDIR(sbuf.st_mode))
    {
        errno = ENOTDIR;
        rc = -1;
        goto errout;
    }
    /* we will keep a copy and keep one in the environment */
    strncpy(pvfs_cwd, newpath, PVFS_PATH_MAX);
    setenv("PWD", newpath, 1);

errout:
    if (newpath != path)
    {
        free(newpath);
    }
    return rc;
}

int pvfs_fchdir(int fd)
{
    int plen;
    pvfs_descriptor *pd;

    /* path is already opened, make sure it is a dir */
    pd = pvfs_find_descriptor(fd);
    if (!pd || !S_ISDIR(pd->s->mode) || !pd->s->dpath)
    {
        errno = EBADF;
        return -1;
    }
    /* basic check for overflow */
    plen = strlen(pd->s->dpath);
    if (plen > PVFS_PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    /* we will keep a copy and keep one in the environment */
    strncpy(pvfs_cwd, pd->s->dpath, PVFS_PATH_MAX);
    setenv("PWD", pd->s->dpath, 1);
    return 0;
}

char *pvfs_getcwd(char *buf, size_t size)
{
    int plen;
    plen = strnlen(pvfs_cwd, PVFS_PATH_MAX);
    /* implement Linux variation */
    if (!buf)
    {
        int bsize = size ? size : plen + 1;
        if (bsize < plen + 1)
        {
            errno = ERANGE;
            return NULL;
        }
        /* malloc space */
        buf = (char *)malloc(bsize);
        if (!buf)
        {
            errno = ENOMEM;
            return NULL;
        }
    }
    else
    {
        if (size == 0)
        {
            errno = EINVAL;
            return NULL;
        }
        if (size < plen + 1)
        {
            errno = ERANGE;
            return NULL;
        }
    }
    strcpy(buf, pvfs_cwd);
    return buf;
}

char *pvfs_get_current_dir_name(void)
{
    int plen;
    char *buf;
    plen = strnlen(pvfs_cwd, PVFS_PATH_MAX);
    buf = (char *)malloc(plen + 1);
    if (!buf)
    {
        errno = ENOMEM;
        return NULL;
    }
    strcpy(buf, pvfs_cwd);
    return buf;
}
/*
 * This is the no-frills old-fashioned version
 * Use at own risk
 */
char *pvfs_getwd(char *buf)
{
    if (!buf)
    {
        errno = EINVAL;
        return NULL;
    }
    strncpy(buf, pvfs_cwd, PVFS_PATH_MAX);
    return buf;
}


/**
 * pvfs_umask
 *
 * Manage a umask ourselves just in case we need to
 * Probably the standard version works fine but
 * In case we get a problem we have it
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

/*
 * Table of PVFS system call versions for use by posix.c
 */
posix_ops pvfs_ops = 
{
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
    .pwrite64 = pvfs_pwrite64,
    .lseek = pvfs_lseek,
    .lseek64 = pvfs_lseek64,
    .truncate = pvfs_truncate,
    .truncate64 = pvfs_truncate64,
    .ftruncate = pvfs_ftruncate,
    .ftruncate64 = pvfs_ftruncate64,
    .fallocate = pvfs_fallocate,
    .close = pvfs_close,
    .stat = pvfs_stat,
    .stat64 = pvfs_stat64,
    .fstat = pvfs_fstat,
    .fstat64 = pvfs_fstat64,
    .fstatat = pvfs_fstatat,
    .fstatat64 = pvfs_fstatat64,
    .lstat = pvfs_lstat,
    .lstat64 = pvfs_lstat64,
    .futimesat = pvfs_futimesat,
    .utimes = pvfs_utimes,
    .utime = pvfs_utime,
    .futimes = pvfs_futimes,
    .dup = pvfs_dup,
    .dup2 = pvfs_dup2,
    .chown = pvfs_chown,
    .fchown = pvfs_fchown,
    .fchownat = pvfs_fchownat,
    .lchown = pvfs_lchown,
    .chmod = pvfs_chmod,
    .fchmod = pvfs_fchmod,
    .fchmodat = pvfs_fchmodat,
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
    .getdents64 = pvfs_getdents64,
    .access = pvfs_access,
    .faccessat = pvfs_faccessat,
    .flock = pvfs_flock,
    .fcntl = pvfs_fcntl,
    .sync = pvfs_sync,
    .fsync = pvfs_fsync,
    .fdatasync = pvfs_fdatasync,
    .fadvise = pvfs_fadvise,
    .fadvise64 = pvfs_fadvise64,
    .statfs = statfs,             /* this one is probably special */
    .statfs64 = pvfs_statfs64,
    .fstatfs = pvfs_fstatfs,
    .fstatfs64 = pvfs_fstatfs64,
    .statvfs = statvfs,             /* this one is probably special */
    .fstatvfs = pvfs_fstatvfs,
    .mknod = pvfs_mknod,
    .mknodat = pvfs_mknodat,
    .sendfile = pvfs_sendfile,
    .sendfile64 = pvfs_sendfile64,
    .setxattr = pvfs_setxattr,
    .lsetxattr = pvfs_lsetxattr,
    .fsetxattr = pvfs_fsetxattr,
    .getxattr = pvfs_getxattr,
    .lgetxattr = pvfs_lgetxattr,
    .fgetxattr = pvfs_fgetxattr,
    .listxattr = pvfs_listxattr,
    .llistxattr = pvfs_llistxattr,
    .flistxattr = pvfs_flistxattr,
    .removexattr = pvfs_removexattr,
    .lremovexattr = pvfs_lremovexattr,
    .fremovexattr = pvfs_fremovexattr,
    .getdtablesize = pvfs_getdtablesize,
    .umask = pvfs_umask,
    .getumask = pvfs_getumask,
    .mmap = pvfs_mmap,
    .munmap = pvfs_munmap,
    .msync = pvfs_msync,
/* these are defined in acl.c and do not really need */
/* a PVFS specific implementation */
#if 0
    .acl_delete_def_file = pvfs_acl_delete_def_file,
    .acl_get_fd = pvfs_acl_get_fd,
    .acl_get_file = pvfs_acl_get_file,
    .acl_set_fd = pvfs_acl_set_fd,
    .acl_set_file = pvfs_acl_set_file,
#endif
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

