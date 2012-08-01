/* 
 * (C) 2011 Clemson University 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PXFS user interface routines
 */

#include "pxfs.h"

extern int pxfs_open(const char *path, int flags, int *fd,
                     pxfs_cb cb, void *cdat, ...)
{
    va_list ap;
    struct pvfs_aiocb *open_acb = NULL;

    if (!path || !fd)
    {
        errno = EINVAL;
        return -1;
    }

    /* alloc a pvfs_cb for use with aio_open */
    open_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!open_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(open_acb, 0, sizeof(struct pvfs_aiocb));

    va_start(ap, cdat);
    if (flags & O_CREAT)
        open_acb->u.open.mode = va_arg(ap, int);
    else
        open_acb->u.open.mode = 0777;
    if (flags & O_HINTS)
        open_acb->u.open.file_creation_param = va_arg(ap, PVFS_hint);
    else
        open_acb->u.open.file_creation_param = PVFS_HINT_NULL;
    va_end(ap);

    open_acb->hints = PVFS_HINT_NULL;
    open_acb->op_code = PVFS_AIO_OPEN_OP;
    open_acb->u.open.path = path;
    open_acb->u.open.flags = flags;
    open_acb->u.open.pdir = NULL;
    open_acb->u.open.fd = fd;
    open_acb->call_back_fn = cb;
    open_acb->call_back_dat = cdat;

    aiocommon_submit_op(open_acb);

    return 0;
}

extern int pxfs_open64(const char *path, int flags, int *fd,
                       pxfs_cb cb, void *cdat, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;

    if (!path || !fd)
    {
        errno = EINVAL;
        return -1;
    }
    va_start(ap, cdat);
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

    return pxfs_open(path, flags, fd, cb, cdat, mode);
}

extern int pxfs_openat(int dirfd, const char *path, int flags, int *fd,
                       pxfs_cb cb, void *cdat, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    int rc;
    struct pvfs_aiocb *open_acb = NULL;

    if (!path || !fd)
    {
        errno = EINVAL;
        return -1;
    }

    va_start(ap, cdat);
    if (flags & O_CREAT)
        mode = va_arg(ap, int);
    else
        mode = 0777;
    if (flags & O_HINTS)
        hints = va_arg(ap, PVFS_hint);
    else
        hints = PVFS_HINT_NULL;
    va_end(ap);

    if (path[0] == '/' || dirfd == AT_FDCWD)
    {
        rc = pxfs_open(path, flags, fd, cb, cdat, mode);
    }
    else
    {
        if (dirfd < 0)
        {
            errno = EBADF;
            return -1;
        }

        /* alloc a pvfs_cb for use with aio_open */
        open_acb = malloc(sizeof(struct pvfs_aiocb));
        if (!open_acb)
        {
            errno = ENOMEM;
            return -1;
        }
        memset(open_acb, 0, sizeof(struct pvfs_aiocb));

        open_acb->u.open.pdir = pvfs_find_descriptor(dirfd);
        if (!(open_acb->u.open.pdir))
        {
            free(open_acb);
            return -1;
        }

        open_acb->hints = PVFS_HINT_NULL;
        open_acb->op_code = PVFS_AIO_OPEN_OP;
        open_acb->u.open.mode = mode;
        open_acb->u.open.file_creation_param = hints;
        open_acb->u.open.path = path;
        open_acb->u.open.flags = flags;
        open_acb->u.open.fd = fd;
        open_acb->call_back_fn = cb;
        open_acb->call_back_dat = cdat;

        aiocommon_submit_op(open_acb);

        return 0;
    }

    return rc;
}

extern int pxfs_openat64(int dirfd, const char *path, int flags, int *fd,
                         pxfs_cb cb, void *cdat, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;

    if (dirfd < 0)
    {
        errno = EBADF;
        return -1;
    }
    
    if (!path || !fd)
    {
        errno = EINVAL;
        return -1;
    }
    va_start(ap, cdat);
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

    return pxfs_openat(dirfd, path, flags, fd, cb, cdat, mode);
}

extern int pxfs_creat(const char *path, mode_t mode, int *fd,
                      pxfs_cb cb, void *cdat, ...)
{
    return pxfs_open(path, O_RDWR | O_CREAT | O_EXCL, fd, cb, cdat, mode);
}

extern int pxfs_creat64(const char *path, mode_t mode, int *fd,
                        pxfs_cb cb, void *cdat, ...)
{
    return pxfs_open64(path, O_RDWR | O_CREAT | O_EXCL, fd, cb, cdat, mode);
}

/*
extern int pxfs_unlink (const char *path, pxfs_cb cb, void *cdat);

extern int pxfs_unlinkat (int dirfd, const char *path, int flags,
                          pxfs_cb cb, void *cdat);
*/

extern int pxfs_rename(const char *oldpath, const char *newpath,
                       pxfs_cb cb, void *cdat)
{
    return 0;
}

extern int pxfs_renameat(int olddirfd, const char *oldpath, int newdirfd,
                         const char *newpath, pxfs_cb cb, void *cdat)
{
    return 0;
}

extern int pxfs_read(int fd, void *buf, size_t count, ssize_t *bcnt,
                     pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd = NULL;
    struct pvfs_aiocb *read_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }

    if (!buf || !bcnt)
    {
        errno = EINVAL;
        return -1;
    }

    read_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!read_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(read_acb, 0, sizeof(struct pvfs_aiocb));

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        free(read_acb);
        return -1;
    }    

    read_acb->hints = PVFS_HINT_NULL;
    read_acb->op_code = PVFS_AIO_IO_OP;
    read_acb->u.io.vector.iov_len = count;
    read_acb->u.io.vector.iov_base = (void *)buf;
    read_acb->u.io.pd = pd;
    read_acb->u.io.which = PVFS_IO_READ;
    read_acb->u.io.offset = pd->s->file_pointer;
    read_acb->u.io.bcnt = bcnt;
    read_acb->call_back_fn = cb;
    read_acb->call_back_dat = cdat;

    aiocommon_submit_op(read_acb);

    return 0;
}

/*
extern int pxfs_pread(int fd, void *buf, size_t count, off_t offset,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_readv(int fd, const struct iovec *vector, int count,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_pread64(int fd, void *buf, size_t count, off64_t offset,
                        ssize_t *bcnt, pxfs_cb cb, void *cdat);
*/

extern int pxfs_write(int fd, const void *buf, size_t count, ssize_t *bcnt,
                      pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd = NULL;
    struct pvfs_aiocb *write_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }

    if (!buf || !bcnt)
    {
        errno = EINVAL;
        return -1;
    }

    write_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!write_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(write_acb, 0, sizeof(struct pvfs_aiocb));

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        free(write_acb);
        return -1;
    }

    write_acb->hints = PVFS_HINT_NULL;
    write_acb->op_code = PVFS_AIO_IO_OP;
    write_acb->u.io.vector.iov_len = count;
    write_acb->u.io.vector.iov_base = (void *)buf;
    write_acb->u.io.pd = pd;
    write_acb->u.io.which = PVFS_IO_WRITE;
    write_acb->u.io.offset = pd->s->file_pointer;
    write_acb->u.io.bcnt = bcnt;
    write_acb->call_back_fn = cb;
    write_acb->call_back_dat = cdat;

    aiocommon_submit_op(write_acb);

    return 0;
}

/*
extern int pxfs_pwrite(int fd, const void *buf, size_t count, off_t offset,
                       ssize_t *bcnt, pxfs_cb cb, void *cdat);

extern int pxfs_writev(int fd, const struct iovec *vector, int count,
                       ssize_t *bcnt , pxfs_cb cb, void *cdat);

extern int pxfs_pwrite64(int fd, const void *buf, size_t count,
                         off64_t offset, ssize_t *bcnt,
                         pxfs_cb cb, void *cdat);
*/



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
