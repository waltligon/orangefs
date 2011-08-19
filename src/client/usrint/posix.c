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

/* this prevents headers from using inlines for 64 bit calls */
#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#endif

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

    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if (is_pvfs_path(path))
    { 
        /* this handles setup of the descriptor */
        flags |= O_NOTPVFS; /* try to open non-pvfs files too */
        return pvfs_open(path, flags, mode, hints);
    }
    else
    {
        int rc;
        struct stat sbuf;
        /* path unknown to FS so open with glibc */
        rc = glibc_ops.open(path, flags & 01777777, mode);
        if (rc < 0)
        {
            return rc;
        }
        /* set up the descriptor manually */
        pd = pvfs_alloc_descriptor(&glibc_ops, rc);
        if (!pd)
        {
            return -1;
        }
        pd->is_in_use = PVFS_FS;
        pd->flags = flags;
        glibc_ops.fstat(rc, &sbuf);
        pd->mode = sbuf.st_mode;
        return pd->fd; 
    }
}

/*
 * open64 wrapper 
 */
int open64(const char *path, int flags, ...)
{ 
    int fd;
    int mode = 0;
    va_list ap;
    va_start(ap, flags);
    if (flags & O_CREAT)
    {
        mode = va_arg(ap, int);
    }
    fd = open(path, flags|O_LARGEFILE, mode); 
    va_end(ap);
    return fd;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    int fd; 
    int mode = 0;
    pvfs_descriptor *pd; 
    va_list ap;
    
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    va_start(ap, flags);
    if (flags & O_CREAT)
    {
        mode = va_arg(ap, int);
    }
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        fd = open(path, flags, mode);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            fd = pd->fsops->openat(pd->true_fd, path, flags, mode);
        }
        else
        {
            errno = EBADF;
            fd = -1;
        }
    }
    va_end(ap);
    return fd;
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    int fd;
    int mode;
    va_list ap;
    va_start(ap, flags);
    if (flags & O_CREAT)
    {
        mode = va_arg(ap, int);
    }
    fd = openat(dirfd, path, flags|O_LARGEFILE, mode); 
    va_end(ap);
    return fd;
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
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if (is_pvfs_path(path))
    {
        return pvfs_ops.unlink(path);
    }
    else
    {
        return glibc_ops.unlink(path);
    }
}

int unlinkat(int dirfd, const char *path, int flag)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        unlink(path);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            rc = pd->fsops->unlinkat(pd->true_fd, path, flag);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

/*
 * rename wrapper
 */
int rename (const char *old, const char *new)
{
    int oldp, newp;
    if (!old || !new)
    {
        errno = EFAULT;
        return -1;
    }
    oldp = is_pvfs_path(old);
    newp = is_pvfs_path(new);
    if(oldp && newp)
    { 
        return pvfs_rename(old, new);
    }
    else if (!oldp && !newp)
    {
        return glibc_ops.rename(old, new);
    }
    else
    {
        errno = EXDEV;
        return -1;
    }
}

int renameat (int oldfd, const char *old, int newfd, const char *new)
{
    pvfs_descriptor *oldpd, *newpd;
    oldpd = pvfs_find_descriptor(oldfd);
    newpd = pvfs_find_descriptor(newfd);
    if (!old || !new)
    {
        errno = EFAULT;
        return -1;
    }
    if (!oldpd || !newpd)
    {
        errno = EBADF;
        return -1;
    }
    if (oldpd->fsops == newpd->fsops)
    {
        return oldpd->fsops->renameat(oldpd->true_fd, old, newpd->true_fd, new);
    }
    else
    {
        errno = EXDEV;
        return -1;
    }
}

/* READING and WRITING SYSTEM CALL */

/*
 * read wrapper 
 */
ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t rc = 0; 
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->read(pd->true_fd, buf, count);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

/*
 * pread wrapper 
 */
ssize_t pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    ssize_t rc = 0; 
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd); 
    if (pd)
    {
        rc = pd->fsops->pread(pd->true_fd, (void *)buf, nbytes, offset); 
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
} 

/*
 * readv wrapper 
 */
ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{ 
    ssize_t rc = 0;
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd); 
    if (pd)
    {
        rc = pd->fsops->readv(pd->true_fd, iov, iovcnt); 
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

/*
 * pread64 wrapper 
 */
ssize_t pread64(int fd, void *buf, size_t nbytes, off64_t offset)
{
    ssize_t rc = 0; 
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd); 
    if (pd)
    {
        rc = pd->fsops->pread64(pd->true_fd, (void *)buf, nbytes, offset); 
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

/*
 * write wrapper 
 */
ssize_t write(int fd, const void *buf, size_t count)
{
    ssize_t rc = 0; 
    pvfs_descriptor *pd; 
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->write(pd->true_fd, (void *)buf, count);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

/*
 * pwrite wrapper 
 */
ssize_t pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
    ssize_t rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->pwrite(pd->true_fd, buf, nbytes, offset);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

/*
 * write wrapper 
 */
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{ 
    ssize_t rc = 0;
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
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

/*
 * pwrite64 wrapper 
 */
ssize_t pwrite64(int fd, const void *buf, size_t nbytes, off64_t offset)
{
    ssize_t rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->pwrite64(pd->true_fd, buf, nbytes, offset);
    }
    else
    {
        errno = EBADF;
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
        errno = EFAULT;
        rc = -1;
    }
    return (off_t)rc;
}

/* 
 * lseek64 wrapper 
 */ 
off64_t lseek64(int fd, off64_t offset, int whence)
{
    off64_t rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->lseek64(pd->true_fd, offset, whence);
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
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_truncate(path, length);
    }
    else
    {
        return glibc_ops.truncate(path, length);
    }
}

int truncate64(const char *path, off64_t length)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_truncate64(path, length);
    }
    else
    {
        return glibc_ops.truncate64(path, length);
    }
}

int ftruncate(int fd, off_t length)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->ftruncate(pd->true_fd, length);
    }
    else
    {
        errno = EBADF;
        rc = (off64_t)-1;
    }
    return rc;
}

int ftruncate64(int fd, off64_t length)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->ftruncate64(pd->true_fd, length);
    }
    else
    {
        errno = EBADF;
        rc = (off64_t)-1;
    }
    return rc;
}

#ifdef _XOPEN_SOURCE
int posix_fallocate(int fd, off_t offset, off_t length)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fallocate(pd->true_fd, offset, length);
    }
    else
    {
        errno = EBADF;
        rc = (off64_t)-1;
    }
    return rc;
}
#endif

/*
 * close wrapper 
 */
int close(int fd)
{
    int rc = 0;
    
    rc = pvfs_free_descriptor(fd);
    return rc;
}

int flush(int fd)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->flush(pd->true_fd);
    }
    else
    {
        errno = EBADF;
        rc = (off64_t)-1;
    }
    return rc;
}

/* various flavors of stat */
int stat(const char *path, struct stat *buf)
{
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
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
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
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
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fstat(pd->true_fd, buf);
    }
    else
    {
        errno = EBADF;
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
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fstat64(pd->true_fd, buf);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int __fxstat64(int ver, int fd, struct stat64 *buf)
{
    return fstat64(fd, buf);
}

int fstatat(int fd, const char *path, struct stat *buf, int flag)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (fd == AT_FDCWD || path[0] == '/')
    {
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            lstat(path, buf);
        }
        else
        {
            stat(path, buf);
        }
    }
    else
    {
        pd = pvfs_find_descriptor(fd);
        if (pd)
        {
            rc = pd->fsops->fstatat(pd->true_fd, path, buf, flag);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

int __fxstatat(int ver, int fd, const char *path, struct stat *buf, int flag)
{
    return fstatat(fd, path, buf, flag);
}

int fstatat64(int fd, const char *path, struct stat64 *buf, int flag)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (fd == AT_FDCWD || path[0] == '/')
    {
        if (flag & AT_SYMLINK_NOFOLLOW)
        {
            lstat64(path, buf);
        }
        else
        {
            stat64(path, buf);
        }
    }
    else
    {
        pd = pvfs_find_descriptor(fd);
        if (pd)
        {
            rc = pd->fsops->fstatat64(pd->true_fd, path, buf, flag);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

int __fxstatat64(int ver, int fd, const char *path, struct stat64 *buf, int flag)
{
    return fstatat64(fd, path, buf, flag);
}

int lstat(const char *path, struct stat *buf)
{
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
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
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
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

int dup(int oldfd)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(oldfd);
    if (pd)
    {
        rc = pd->fsops->dup(pd->true_fd);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int dup2(int oldfd, int newfd)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(oldfd);
    if (pd)
    {
        rc = pd->fsops->dup2(pd->true_fd, newfd);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int chown(const char *path, uid_t owner, gid_t group)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_chown(path, owner, group);
    }
    else
    {
        return glibc_ops.chown(path, owner, group);
    }
}

int fchown(int fd, uid_t owner, gid_t group)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fchown(pd->true_fd, owner, group);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flag)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        chown(path, owner, group);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            rc = pd->fsops->fchownat(pd->true_fd, path, owner, group, flag);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

int lchown(const char *path, uid_t owner, gid_t group)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_lchown(path, owner, group);
    }
    else
    {
        return glibc_ops.lchown(path, owner, group);
    }
}

int chmod(const char *path, mode_t mode)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_chmod(path, mode);
    }
    else
    {
        return glibc_ops.chmod(path, mode);
    }
}

int fchmod(int fd, mode_t mode)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fchmod(pd->true_fd, mode);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flag)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        chmod(path, mode);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            rc = pd->fsops->fchmodat(pd->true_fd, path, mode, flag);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

int mkdir(const char *path, mode_t mode)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_mkdir(path, mode);
    }
    else
    {
        return glibc_ops.mkdir(path, mode);
    }
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        mkdir(path, mode);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            rc = pd->fsops->mkdirat(pd->true_fd, path, mode);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

int rmdir(const char *path)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_rmdir(path);
    }
    else
    {
        return glibc_ops.rmdir(path);
    }
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_readlink(path, buf, bufsiz);
    }
    else
    {
        return glibc_ops.readlink(path, buf, bufsiz);
    }
}

int readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        readlink(path, buf, bufsiz);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            rc = pd->fsops->readlinkat(pd->true_fd, path, buf, bufsiz);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

ssize_t symlink(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(newpath))
    { 
        return pvfs_symlink(oldpath, newpath);
    }
    else
    {
        return glibc_ops.symlink(oldpath, newpath);
    }
}

int symlinkat(const char *oldpath, int newdirfd, const char *newpath)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (!oldpath || !newpath)
    {
        errno = EFAULT;
        return -1;
    }
    if (newdirfd == AT_FDCWD || newpath[0] == '/')
    {
        symlink(oldpath, newpath);
    }
    else
    {
        pd = pvfs_find_descriptor(newdirfd);
        if (pd)
        {
            rc = pd->fsops->symlinkat(oldpath, pd->true_fd, newpath);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

ssize_t link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(oldpath))
    { 
        return pvfs_link(oldpath, newpath);
    }
    else
    {
        return glibc_ops.link(oldpath, newpath);
    }
    return -1;
}

int linkat(int olddirfd, const char *old,
           int newdirfd, const char *new, int flags)
{
    pvfs_descriptor *oldpd, *newpd;
    oldpd = pvfs_find_descriptor(olddirfd);
    newpd = pvfs_find_descriptor(newdirfd);
    if (!old || !new)
    {
        errno = EFAULT;
        return -1;
    }
    if (!oldpd || !newpd)
    {
        errno = EBADF;
        return -1;
    }
    if (oldpd->fsops == newpd->fsops)
    {
        return oldpd->fsops->linkat(oldpd->true_fd, old,
                                    newpd->true_fd, new, flags);
    }
    else
    {
        errno = EXDEV;
        return -1;
    }
}

int posix_readdir(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    int rc;
    pvfs_descriptor *pd;
    
    if (!dirp)
    {
        errno = EFAULT;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->readdir(pd->true_fd, dirp, count);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    if (!dirp)
    {
        errno = EFAULT;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->getdents(pd->true_fd, dirp, count);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int getdents64(unsigned int fd, struct dirent64 *dirp, unsigned int count)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    if (!dirp)
    {
        errno = EFAULT;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->getdents64(pd->true_fd, dirp, count);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

/* linux discourages using readdir system calls, so for now
 * we will leave them out - there are stdio calls that can
 * be used
 */

int access(const char *path, int mode)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_access(path, mode);
    }
    else
    {
        return glibc_ops.access(path, mode);
    }
}

int faccessat(int dirfd, const char *path, int mode, int flags)
{
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        access(path, mode);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            rc = pd->fsops->faccessat(pd->true_fd, path, mode, flags);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

int flock(int fd, int op)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->flock(pd->true_fd, op);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int fcntl(int fd, int cmd, ...)
{
    int rc = 0;
    long arg;
    struct flock *lock;
    pvfs_descriptor *pd;
    va_list ap;
    
    va_start(ap, cmd);
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        switch (cmd)
        {
        case F_GETLK:
        case F_SETLK:
        case F_SETLKW:
            lock = va_arg(ap, struct flock *);
            rc = pd->fsops->fcntl(pd->true_fd, cmd, lock);
            break;
        default:
            arg = va_arg(ap, long);
            rc = pd->fsops->fcntl(pd->true_fd, cmd, arg);
            break;
        }
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    va_end(ap);
    return rc;
}

void sync(void)
{
    pvfs_sync();
    glibc_ops.sync();
}

int fsync(int fd)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fsync(pd->true_fd);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int fdatasync(int fd)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fdatasync(pd->true_fd);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int posix_fadvise(int fd, off_t offset, off_t length, int advice)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fadvise(pd->true_fd, offset, length, advice);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int fadvise(int fd, off_t offset, off_t len, int advice)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fadvise(pd->true_fd, offset, len, advice);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int fadvise64(int fd, off64_t offset, off64_t len, int advice)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fadvise64(pd->true_fd, offset, len, advice);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int statfs(const char *path, struct statfs *buf)
{
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_statfs(path, buf);
    }
    else
    {
        return glibc_ops.statfs(path, buf);
    }
}

int statfs64(const char *path, struct statfs64 *buf)
{
    if (!path || !buf)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_statfs64(path, buf);
    }
    else
    {
        return glibc_ops.statfs64(path, buf);
    }
}

int fstatfs(int fd, struct statfs *buf)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    if (!buf)
    {
        errno = EFAULT;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fstatfs(pd->true_fd, buf);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int fstatfs64(int fd, struct statfs64 *buf)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    if (!buf)
    {
        errno = EFAULT;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fstatfs64(pd->true_fd, buf);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_mknod(path, mode, dev);
    }
    else
    {
        return glibc_ops.mknod(path, mode, dev);
    }
}

int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{            
    int rc = 0; 
    pvfs_descriptor *pd; 
    
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if (dirfd == AT_FDCWD || path[0] == '/')
    {
        mknod(path, mode, dev);
    }
    else
    {
        pd = pvfs_find_descriptor(dirfd);
        if (pd)
        {
            rc = pd->fsops->mknodat(pd->true_fd, path, mode, dev);
        }
        else
        {
            errno = EBADF;
            rc = -1;
        }
    }
    return rc;
}

ssize_t sendfile(int outfd, int infd, off_t *offset, size_t count)
{
    return sendfile64(outfd, infd, (off64_t *)offset, count);
}

ssize_t sendfile64(int outfd, int infd, off64_t *offset, size_t count)
{
    int rc = 0;
    pvfs_descriptor *inpd, *outpd;
    
    inpd = pvfs_find_descriptor(infd);
    outpd = pvfs_find_descriptor(outfd);
    if (inpd && outpd)
    {
        rc = inpd->fsops->sendfile64(outpd->true_fd, inpd->true_fd,
                                     offset, count);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int setxattr(const char *path, const char *name,
             const void *value, size_t size, int flags)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_setxattr(path, name, value, size, flags);
    }
    else
    {
        return glibc_ops.setxattr(path, name, value, size, flags);
    }
}

int lsetxattr(const char *path, const char *name,
              const void *value, size_t size, int flags)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_lsetxattr(path, name, value, size, flags);
    }
    else
    {
        return glibc_ops.lsetxattr(path, name, value, size, flags);
    }
}

int fsetxattr(int fd, const char *name,
              const void *value, size_t size, int flags)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fsetxattr(pd->true_fd, name, value, size, flags);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int getxattr(const char *path, const char *name,
             void *value, size_t size)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_getxattr(path, name, value, size);
    }
    else
    {
        return glibc_ops.getxattr(path, name, value, size);
    }
}

int lgetxattr(const char *path, const char *name,
              void *value, size_t size)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_lgetxattr(path, name, value, size);
    }
    else
    {
        return glibc_ops.lgetxattr(path, name, value, size);
    }
}

int fgetxattr(int fd, const char *name, void *value,
              size_t size)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fgetxattr(pd->true_fd, name, value, size);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int listxattr(const char *path, char *list, size_t size)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_listxattr(path, list, size);
    }
    else
    {
        return glibc_ops.listxattr(path, list, size);
    }
}

int llistxattr(const char *path, char *list, size_t size)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_llistxattr(path, list, size);
    }
    else
    {
        return glibc_ops.llistxattr(path, list, size);
    }
}

int flistxattr(int fd, char *list, size_t size)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->flistxattr(pd->true_fd, list, size);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int removexattr(const char *path, const char *name)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_removexattr(path, name);
    }
    else
    {
        return glibc_ops.removexattr(path, name);
    }
}

int lremovexattr(const char *path, const char *name)
{
    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    if(is_pvfs_path(path))
    { 
        return pvfs_lremovexattr(path, name);
    }
    else
    {
        return glibc_ops.lremovexattr(path, name);
    }
}

int fremovexattr(int fd, const char *name)
{
    int rc = 0;
    pvfs_descriptor *pd;
    
    pd = pvfs_find_descriptor(fd);
    if (pd)
    {
        rc = pd->fsops->fremovexattr(pd->true_fd, name);
    }
    else
    {
        errno = EBADF;
        rc = -1;
    }
    return rc;
}

int chdir(const char *path)
{
    return pvfs_chdir(path);
}

int fchdir(int fd)
{
    return pvfs_fchdir(fd);
}

char *getcwd(char *buf, size_t size)
{
    return pvfs_getcwd(buf, size);
}

char *get_current_dir_name(void)
{
    return pvfs_get_current_dir_name();
}

char *getwd(char *buf)
{
    return pvfs_getwd(buf);
}

mode_t umask(mode_t mask)
{
    return pvfs_umask(mask);
}

mode_t getumask(void)
{
    return pvfs_getumask();
}

int getdtablesize(void)
{
    return pvfs_getdtablesize();
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
