/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - wrappers for posix system calls
 */
#include <usrint.h>
#include <linux/dirent.h>
#include <posix-ops.h>
#include <posix-pvfs.h>
#include <openfile-util.h>

/*
 * SYSTEM CALLS
 */

/*
 * open wrapper
 */ 
int open(const char *path, int flags, ...)
{ 
    va_list ap; 
    mode_t mode = 0; 
    PVFS_hint hints;  /* need to figure out how to set default */
    pvfs_descriptor *pd;
    
    va_start(ap, flags); 
    if (flags & O_CREAT)
        mode = va_arg(ap, mode_t); 
    else
        mode = 0777;
    if (flags & O_HINTS)
        hints = va_arg(ap, PVFS_hint);
    else
       hints = PVFS_HINT_NULL;
    va_end(ap); 

    if (is_pvfs_path(path))
    { 
        return pvfs_open(path, flags, mode, hints);
    }
    else
    {
        pd = pvfs_alloc_descriptor(&glibc_ops);
        pd->posix_fd = pd->fsops->open(path, flags, mode);
        pd->flags = flags;
        pd->is_in_use = 1;
        return pd->fd; 
    }
}

/*
 * open64 wrapper 
 */
int open64(const char *path, int flags, ...)
{ 
    va_list ap; 
    mode_t mode = 0; 
    PVFS_hint hints;  /* need to figure out how to set default */
    
    va_start(ap, flags); 
    if (flags & O_HINTS)
        hints = va_arg(ap, PVFS_hint);
    else
        hints = PVFS_HINT_NULL;
    va_end(ap); 

    return open(path, flags|O_LARGEFILE, mode, hints); 
}

int openat(int dirfd, const char *path, int flags, ...)
{
}

int openat64(int dirfd, const char *path, int flags, ...)
{
}

/*
 * creat wrapper
 */ 
int creat(const char *path, mode_t mode)
{ 
    return open(path, O_CREAT|O_WRONLY|O_TRUNC, mode); 
}

/*
 * creat64 wrapper
 */ 
int creat64(const char *path, mode_t mode)
{ 
    return open64(path, O_CREAT|O_WRONLY|O_TRUNC, mode); 
}

/*
 * unlink wrapper 
 */
int unlink(const char *path)
{
    if (is_pvfs_path(path)) {
        return pvfs_ops.unlink(path);
    }
    else {
        return glibc_ops.unlink(path);
    }
}

int unlinkat(int dirfd, const char *path, int flag)
{
}

/*
 * rename wrapper
 */
int rename (const char *old, const char *new)
{
}

/* READING and WRITING SYSTEM CALL */

/*
 * read wrapper 
 */
ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t rc; 
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->read(fd, buf, count);
        if (rc > 0)
            pd->file_pointer += rc;
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

/*
 * write wrapper 
 */
ssize_t write(int fd, const void *buf, size_t count)
{
    ssize_t rc; 
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->write(fd, (void *)buf, count);
        if (rc > 0)
            pd->file_pointer += rc;
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

/*
 * write wrapper 
 */
ssize_t pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    ssize_t rc; 
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd); 
    if (pd)
    {
        rc = pd->fsops->pread(fd, (void *)buf, nbytes, offset); 
        if (rc > 0)
            pd->file_pointer += rc;
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
} 

/*
 * write wrapper 
 */
ssize_t pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
    ssize_t rc;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->pwrite(fd, buf, nbytes, offset);
        if (rc > 0)
            pd->file_pointer += rc;
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

/*
 * write wrapper 
 */
ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{ 
    ssize_t rc;
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd); 
    if (pd)
    {
        rc = pd->fsops->readv(fd, iov, iovcnt); 
        if (rc > 0)
            pd->file_pointer += rc;
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

/*
 * write wrapper 
 */
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{ 
    ssize_t rc;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->writev(fd, iov, iovcnt);
        if (rc > 0)
            pd->file_pointer += rc;
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

/* SEEK system calls */

/*
 * lseek wrapper 
 */
off_t lseek(int fd, off_t offset, int whence)
{
    off64_t rc = lseek64(fd, (off64_t)offset, whence);
    if (rc & 0xffffffff00000000LLU)
    {
        errno = EINVAL;
        rc = -1;
    }
    return (off_t)rc;
}

/* 
 * lseek64 wrapper 
 */ 
off64_t lseek64(int fd, off64_t offset, int whence)
{
    off64_t rc;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->lseek64(fd, offset, whence);
        if (rc != -1)
            pd->file_pointer = rc;
    }
    else
    {
        errno = EBADF;
        rc = (off64_t)-1;
    }
    return rc;
}

int truncate(const char *path, off_t length)
{
}

int truncate64(const char *path, off64_t length)
{
}

int ftruncate(int fd, off_t length)
{
}

int ftruncate64(int fd, off64_t length)
{
}

int posix_fallocate(int fd, off_t offset, off_t length)
{
}

int posix_fadvize(int fd, off_t offset, off_t length, int advice)
{
}

/*
 * close wrapper 
 */
int close(int fd)
{
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        pd->fsops->flush(pd->posix_fd);
        pd->fsops->close(pd->posix_fd);
        pvfs_free_descriptor(pd);
        return 0;
    }
    else
    {
        errno = EBADF;
        return -1;
    }
}

int flush(int fd)
{
}

/* various flavors of stat */
int stat(const char *path, struct stat *buf)
{
    if(is_pvfs_path(path))
    { 
        return pvfs_stat(path, buf);
    }
    else
    {
        return glibc_ops.stat(path, buf);
    }
}

int __xstat(int ver, const char *path, struct stat *buf)
{
    return stat(path, buf);
}

int stat64(const char *path, struct stat64 *buf)
{
    if(is_pvfs_path(path))
    { 
        return pvfs_stat64(path, buf);
    }
    else
    {
        return glibc_ops.stat64(path, buf);
    }
}

int __xstat64(int ver, const char *path, struct stat64 *buf)
{
    return stat64(path, buf);
}

int fstat(int fd, struct stat *buf)
{
    int rc;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fstat(pd->posix_fd, buf);
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

int __fxstat(int ver, int fd, struct stat *buf)
{
    return fstat(fd, buf);
}

int fstat64(int fd, struct stat64 *buf)
{
    int rc;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fstat64(pd->posix_fd, buf);
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

int __fxstat64(int ver, int fd, struct stat64 *buf)
{
    return fstat64(fd, buf);
}

int lstat(const char *path, struct stat *buf)
{
    if(is_pvfs_path(path))
    { 
        return pvfs_lstat(path, buf);
    }
    else
    {
        return glibc_ops.lstat(path, buf);
    }
}

int __lxstat(int ver, const char *path, struct stat *buf)
{
    return lstat(path, buf);
}

int lstat64(const char *path, struct stat64 *buf)
{
    if(is_pvfs_path(path))
    { 
        return pvfs_lstat64(path, buf);
    }
    else
    {
        return glibc_ops.lstat64(path, buf);
    }
}

int __lxstat64(int ver, const char *path, struct stat64 *buf)
{
    return lstat64(path, buf);
}

int getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    return -1;
}

#if 0
int dup()
{
}

int dup2()
{
}

int chown()
{
}

int fchown()
{
}

int fchownat()
{
}

int lchown()
{
}

int chmod()
{
}

int fchmod()
{
}

int fchmodat()
{
}

int lchmod()
{
}

int mkdir()
{
}

int mdirat()
{
}

int rmdir()
{
}

ssize_t readlink()
{
}

int readlinkat()
{
}

ssize_t symlink()
{
}

int symlinkat()
{
}

ssize_t link()
{
}

int linkat()
{
}

#endif

/* linux discourages using readdir system calls, so for now
 * we will leave them out - there are stdio calls that can
 * be used
 */

#if 0

int access (int fd, int op)
{
}

int faccessat (int fd, int op)
{
}

int flock (int fd, int op)
{
}

int fcntl (int fd, int op)
{
}

int sync (int fd, int op)
{
}

int fsync (int fd, int op)
{
}

int fdatasync (int fd, int op)
{
}

int umask (int fd, int op)
{
}

int getumask (int fd, int op)
{
}

int getdtablesize (int fd, int op)
{
}

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
