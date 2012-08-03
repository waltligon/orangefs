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

/* actual implementations of the read/write functions */

static int pxfs_rdwr64(int fd,
                       void *buf,
                       size_t size,
                       off64_t offset,
                       ssize_t *bcnt,
                       pxfs_cb cb,
                       void *cdat,
                       int which,
                       int advance_fp);

static int pxfs_rdwrv(int fd,
                      const struct iovec *vector,
                      int count,
                      ssize_t *bcnt,
                      pxfs_cb  cb,
                      void *cdat,
                      int which);

/**
 *
 **/

/**
 * pxfs_open
 */
extern int pxfs_open(const char *path, int flags, int *fd,
                     pxfs_cb cb, void *cdat, ...)
{
    int rc;
    va_list ap;
    char *newpath;
    struct pvfs_aiocb *open_acb = NULL;

    if (!path || !fd)
    {
        errno = EINVAL;
        return -1;
    }

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }
    if (newpath == path)
    {
        newpath = malloc(strlen(path) + 1);
        if (!newpath)
        {
            errno = ENOMEM;
            return -1;
        }
        strcpy(newpath, path);
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

    rc = split_pathname(newpath, 0, &(open_acb->u.open.directory),
                        &(open_acb->u.open.filename));
    if (rc < 0)
    {
        return -1;
    }   

    open_acb->hints = PVFS_HINT_NULL;
    open_acb->op_code = PVFS_AIO_OPEN_OP;
    open_acb->u.open.path = newpath;
    open_acb->u.open.flags = flags;
    open_acb->u.open.pdir = NULL;
    open_acb->u.open.fd = fd;
    open_acb->call_back_fn = cb;
    open_acb->call_back_dat = cdat;

    aiocommon_submit_op(open_acb);

    if (newpath != path)
    {
        free(newpath);
    }

    return 0;
}

/**
 * pxfs_open64
 */
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

/*
extern int pxfs_openat(int dirfd, const char *path, int flags, int *fd,
                       pxfs_cb cb, void *cdat, ...)
{
    return 0;
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
*/

/**
 * pxfs_creat
 */
extern int pxfs_creat(const char *path, mode_t mode, int *fd,
                      pxfs_cb cb, void *cdat, ...)
{
    return pxfs_open(path, O_RDWR | O_CREAT | O_EXCL, fd, cb, cdat, mode);
}

/**
 * pxfs_creat64
 */
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

/**
 * pxfs_rename
 */
extern int pxfs_rename(const char *oldpath, const char *newpath,
                       pxfs_cb cb, void *cdat)
{
    int rc;
    char *abs_oldpath, *abs_newpath;
    struct pvfs_aiocb *rename_acb = NULL;

    if (!oldpath | !newpath)
    {
        errno = EINVAL;
        return -1;
    }

    rename_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!rename_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(rename_acb, 0, sizeof(struct pvfs_aiocb));
    
    abs_oldpath = pvfs_qualify_path(oldpath);
    if (!abs_oldpath)
    {
        return -1;
    }

    abs_newpath = pvfs_qualify_path(newpath);
    if (!abs_newpath)
    {
        return -1;
    }

    rc = split_pathname(abs_oldpath, 0, &(rename_acb->u.rename.olddir),
                        &(rename_acb->u.rename.oldname));
    if (rc < 0)
    {
        return -1;
    }
    
    rc = split_pathname(abs_newpath, 0, &(rename_acb->u.rename.newdir),
                        &(rename_acb->u.rename.newname));
    if (rc < 0)
    {
        return -1;
    }

    rename_acb->hints = PVFS_HINT_NULL;
    rename_acb->op_code = PVFS_AIO_RENAME_OP;
    rename_acb->u.rename.oldpdir = NULL;
    rename_acb->u.rename.newpdir = NULL;
    rename_acb->call_back_fn = cb;
    rename_acb->call_back_dat = cdat;

    aiocommon_submit_op(rename_acb);

    if (abs_oldpath != oldpath)
    {
        free(abs_oldpath);
    }
    if (abs_newpath != newpath)
    {
        free(abs_newpath);
    }

    return 0;
}

/*
extern int pxfs_renameat(int olddirfd, const char *oldpath, int newdirfd,
                         const char *newpath, pxfs_cb cb, void *cdat)
{
}
*/

extern int pxfs_read(int fd, const void *buf, size_t count, ssize_t *bcnt,
                     pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd = NULL;

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

    return pxfs_rdwr64(fd, (void *)buf, count, pd->s->file_pointer, bcnt,
                        cb, cdat, PVFS_IO_READ, 1);
}

extern int pxfs_pread(int fd, const void *buf, size_t count, off_t offset,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, (off64_t)offset, bcnt,
                        cb, cdat, PVFS_IO_READ, 0);
}

extern int pxfs_readv(int fd, const struct iovec *vector, int count,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwrv(fd, vector, count, bcnt, cb, cdat, PVFS_IO_READ);
}

extern int pxfs_pread64(int fd, const void *buf, size_t count, off64_t offset,
                        ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, offset, bcnt, cb,
                       cdat, PVFS_IO_READ, 0);
}

extern int pxfs_write(int fd, const void *buf, size_t count, ssize_t *bcnt,
                      pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd = NULL;

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
    /* check for append mode */
    if (pd->s->flags & O_APPEND)
    {
        /* WHAT */
        return 0;
    }
    else
    {
        return pxfs_rdwr64(fd, (void *)buf, count, pd->s->file_pointer, bcnt,
                           cb, cdat, PVFS_IO_WRITE, 0);
    }
}

extern int pxfs_pwrite(int fd, const void *buf, size_t count, off_t offset,
                       ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, (off64_t)offset, bcnt,
                        cb, cdat, PVFS_IO_WRITE, 0);
}

extern int pxfs_writev(int fd, const struct iovec *vector, int count,
                       ssize_t *bcnt , pxfs_cb cb, void *cdat)
{
    return pxfs_rdwrv(fd, vector, count, bcnt, cb, cdat, PVFS_IO_WRITE);
}

extern int pxfs_pwrite64(int fd, const void *buf, size_t count,
                         off64_t offset, ssize_t *bcnt,
                         pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, offset, bcnt, cb,
                       cdat, PVFS_IO_WRITE, 0);
}

static int pxfs_rdwr64(int fd,
                       void *buf,
                       size_t size,
                       off64_t offset,
                       ssize_t *bcnt,
                       pxfs_cb cb, 
                       void *cdat,
                       int which,
                       int advance_fp)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *io_acb = NULL;

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

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }

    /* Ensure descriptor is used for the correct type of access */
    if ((which == PVFS_IO_READ &&
            (O_WRONLY == (pd->s->flags & O_ACCMODE))) ||
        (which == PVFS_IO_WRITE &&
            (O_RDONLY == (pd->s->flags & O_ACCMODE))))
    {
        errno = EBADF;
        return -1;
    }

    io_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!io_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(io_acb, 0, sizeof(struct pvfs_aiocb));

    io_acb->u.io.vector = malloc(sizeof(struct iovec));
    if(!(io_acb->u.io.vector))
    {
        errno = -ENOMEM;
        return -1;
    }

    io_acb->hints = PVFS_HINT_NULL;
    io_acb->op_code = PVFS_AIO_IO_OP;
    io_acb->u.io.vector->iov_len = size;
    io_acb->u.io.vector->iov_base = (void *)buf;
    io_acb->u.io.pd = pd;
    io_acb->u.io.which = which;
    io_acb->u.io.advance_fp = advance_fp;
    io_acb->u.io.offset = offset;
    io_acb->u.io.bcnt = bcnt;
    io_acb->call_back_fn = cb;
    io_acb->call_back_dat = cdat;

    aiocommon_submit_op(io_acb);

    return 0;
}

static int pxfs_rdwrv(int fd,
                      const struct iovec *vector,
                      int count,
                      ssize_t *bcnt,
                      pxfs_cb  cb,
                      void *cdat,
                      int which)

{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *io_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }

    if (!vector || !bcnt)
    {
        errno = EINVAL;
        return -1;
    }

    /* find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        errno = EBADF;
        return -1;
    }    

    /* Ensure descriptor is used for the correct type of access */
    if ((which == PVFS_IO_READ &&
            (O_WRONLY == (pd->s->flags & O_ACCMODE))) ||
        (which == PVFS_IO_WRITE &&
            (O_RDONLY == (pd->s->flags & O_ACCMODE))))
    {
        errno = EBADF;
        return -1;
    }

    io_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!io_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(io_acb, 0, sizeof(struct pvfs_aiocb));

    io_acb->hints = PVFS_HINT_NULL;
    io_acb->op_code = PVFS_AIO_IOV_OP;
    io_acb->u.io.vector = (struct iovec *)vector;
    io_acb->u.io.count = count;
    io_acb->u.io.pd = pd;
    io_acb->u.io.which = which;
    io_acb->u.io.offset = pd->s->file_pointer;
    io_acb->u.io.bcnt = bcnt;
    io_acb->call_back_fn = cb;
    io_acb->call_back_dat = cdat;

    aiocommon_submit_op(io_acb);

    return 0;
}
/*
extern int pxfs_truncate(const char *path, off_t length,
                         pxfs_cb cb, void *cdat);

extern int pxfs_truncate64 (const char *path, off64_t length,
                            pxfs_cb cb, void *cdat);

extern int pxfs_fallocate(int fd, off_t offset, off_t length,
                          pxfs_cb cb, void *cdat);
*/

/**
 * pxfs_ftruncate
 */
extern int pxfs_ftruncate(int fd, off_t length, pxfs_cb cb, void *cdat)
{
    return pxfs_ftruncate64(fd, (off64_t)length, cb, cdat);
}

/**
 * pxfs_ftruncate64
 */
extern int pxfs_ftruncate64(int fd, off64_t length, pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *truncate_acb = NULL;

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

    truncate_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!truncate_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(truncate_acb, 0, sizeof(struct pvfs_aiocb));

    truncate_acb->hints = PVFS_HINT_NULL;
    truncate_acb->op_code = PVFS_AIO_TRUNC_OP;
    truncate_acb->u.trunc.pd = pd;
    truncate_acb->u.trunc.length = length;
    truncate_acb->call_back_fn = cb;
    truncate_acb->call_back_dat = cdat;

    aiocommon_submit_op(truncate_acb);

    return 0;
}

/*
extern int pxfs_close( int fd , pxfs_cb cb, void *cdat);

extern int pxfs_flush(int fd, pxfs_cb cb, void *cdat);

extern int pxfs_stat(const char *path, struct stat *buf,
                     pxfs_cb cb, void *cdat);

extern int pxfs_stat64(const char *path, struct stat64 *buf,
                       pxfs_cb cb, void *cdat);

extern int pxfs_stat_mask(const char *path, struct stat *buf,
                          uint32_t mask, pxfs_cb cb, void *cdat);
*/

/**
 * pxfs_fstat
 */
extern int pxfs_fstat(int fd, struct stat *buf, pxfs_cb cb, void *cdat)
{
    return pxfs_fstat_mask(fd, buf, PVFS_ATTR_DEFAULT_MASK, cb, cdat);
}

/**
 * pxfs_fstat64
 */
extern int pxfs_fstat64(int fd, struct stat64 *buf, pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;    
    struct pvfs_aiocb *stat_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    if (!buf)
    {
        errno = EINVAL;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    stat_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!stat_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(stat_acb, 0, sizeof(struct pvfs_aiocb));

    stat_acb->hints = PVFS_HINT_NULL;
    stat_acb->op_code = PVFS_AIO_STAT64_OP;
    stat_acb->u.stat.pd = pd;
    stat_acb->u.stat.buf = (void *)buf;
    stat_acb->u.stat.mask = PVFS_ATTR_DEFAULT_MASK;
    stat_acb->call_back_fn = cb;
    stat_acb->call_back_dat = cdat;

    aiocommon_submit_op(stat_acb);

    return 0;
}

/*
extern int pxfs_fstatat(int fd, const char *path, struct stat *buf,
                        int flag, pxfs_cb cb, void *cdat);

extern int pxfs_fstatat64(int fd, const char *path, struct stat64 *buf,
                          int flag, pxfs_cb cb, void *cdat);
*/

/**
 * pxfs_fstat_mask
 */
extern int pxfs_fstat_mask(int fd, struct stat *buf, uint32_t mask,
                           pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *stat_acb = NULL;
    
    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    if (!buf)
    {
        errno = EINVAL;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    mask &= PVFS_ATTR_DEFAULT_MASK;

    stat_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!stat_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(stat_acb, 0, sizeof(struct pvfs_aiocb));

    stat_acb->hints = PVFS_HINT_NULL;
    stat_acb->op_code = PVFS_AIO_STAT_OP;
    stat_acb->u.stat.pd = pd;
    stat_acb->u.stat.buf = (void *)buf;
    stat_acb->u.stat.mask = mask;
    stat_acb->call_back_fn = cb;
    stat_acb->call_back_dat = cdat;

    aiocommon_submit_op(stat_acb);

    return 0;    
}

/*
extern int pxfs_lstat(const char *path, struct stat *buf,
                      pxfs_cb cb, void *cdat);

extern int pxfs_lstat64(const char *path, struct stat64 *buf,
                        pxfs_cb cb, void *cdat);

extern int pxfs_lstat_mask(const char *path, struct stat *buf, uint32_t mask,
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
