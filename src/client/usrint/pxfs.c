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

#define PVFS_ATTR_DEFAULT_MASK \
(PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_SIZE | PVFS_ATTR_SYS_BLKSIZE)

static mode_t mask_val = 0022; /* implements umask for pvfs library */

/* actual implementations of the read/write functions */

static int pxfs_rdwr64(int fd,
                       void *buf,
                       size_t size,
                       off64_t offset,
                       ssize_t *bcnt,
                       pxfs_cb cb,
                       void *cdat,
                       int which,
                       int append_mode,
                       int advance_fp);

static int pxfs_rdwrv(int fd,
                      const struct iovec *vector,
                      int count,
                      ssize_t *bcnt,
                      pxfs_cb  cb,
                      void *cdat,
                      int which);

/**
 ***
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

/**
 * pxfs_unlink
 */
extern int pxfs_unlink(const char *path, pxfs_cb cb, void *cdat)
{
    char *newpath;
    int rc;
    struct pvfs_aiocb *unlink_acb;

    if (!path)
    {
        errno = EINVAL;
        return -1;
    }

    unlink_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!unlink_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(unlink_acb, 0, sizeof(struct pvfs_aiocb));

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }

    rc = split_pathname(newpath, 0, &unlink_acb->u.remove.directory,
                        &unlink_acb->u.remove.filename);
    if (rc < 0)
    {
        return -1;
    }

    unlink_acb->hints = PVFS_HINT_NULL;
    unlink_acb->op_code = PVFS_AIO_REMOVE_OP;
    unlink_acb->u.remove.pdir = NULL;
    unlink_acb->u.remove.dirflag = 0;
    unlink_acb->call_back_fn = cb;
    unlink_acb->call_back_dat = cdat;

    aiocommon_submit_op(unlink_acb);

    if (newpath != path)
    {
        free(newpath);
    }

    return 0;
}

/*
extern int pxfs_unlinkat(int dirfd, const char *path, int flags,
                         pxfs_cb cb, void *cdat);
*/

/**
 * pxfx_rename
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

/**
 * pxfs_read
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
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }    

    return pxfs_rdwr64(fd, (void *)buf, count, pd->s->file_pointer, bcnt,
                       cb, cdat, PVFS_IO_READ, 0, 1);
}

/**
 * pxfs_pread
 */
extern int pxfs_pread(int fd, const void *buf, size_t count, off_t offset,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, (off64_t)offset, bcnt,
                       cb, cdat, PVFS_IO_READ, 0, 0);
}

/**
 * pxfs_readv
 */
extern int pxfs_readv(int fd, const struct iovec *vector, int count,
                      ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwrv(fd, vector, count, bcnt, cb, cdat, PVFS_IO_READ);
}

/**
 * pxfs_pread64
 */
extern int pxfs_pread64(int fd, const void *buf, size_t count, off64_t offset,
                        ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, offset, bcnt, cb,
                       cdat, PVFS_IO_READ, 0, 0);
}

/**
 * pxfs_write
 */
extern int pxfs_write(int fd, const void *buf, size_t count, ssize_t *bcnt,
                      pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd = NULL;
    int append_mode = 0;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }

    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    if (pd->s->flags & O_APPEND)
    {
        append_mode = 1;
    }

    return pxfs_rdwr64(fd, (void *)buf, count, pd->s->file_pointer, bcnt, 
                       cb, cdat, PVFS_IO_WRITE, append_mode, 1);
}

/**
 * pxfs_pwrite
 */
extern int pxfs_pwrite(int fd, const void *buf, size_t count, off_t offset,
                       ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, (off64_t)offset, bcnt,
                        cb, cdat, PVFS_IO_WRITE, 0, 0);
}

/**
 * pxfs_writev
 */
extern int pxfs_writev(int fd, const struct iovec *vector, int count,
                       ssize_t *bcnt , pxfs_cb cb, void *cdat)
{
    return pxfs_rdwrv(fd, vector, count, bcnt, cb, cdat, PVFS_IO_WRITE);
}

/**
 * pxfs_pwrite64
 */
extern int pxfs_pwrite64(int fd, const void *buf, size_t count,
                         off64_t offset, ssize_t *bcnt,
                         pxfs_cb cb, void *cdat)
{
    return pxfs_rdwr64(fd, (void *)buf, count, offset, bcnt, cb,
                       cdat, PVFS_IO_WRITE, 0, 0);
}

/**
 * pxfs_rdwr64 - implements pxfs read and write operations
 */
static int pxfs_rdwr64(int fd,
                       void *buf,
                       size_t size,
                       off64_t offset,
                       ssize_t *bcnt,
                       pxfs_cb cb, 
                       void *cdat,
                       int which,
                       int append_mode, /* 1 for append writes */
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
    if (!pd || pd->is_in_use != PVFS_FS)
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
    io_acb->u.io.count = 1;
    io_acb->u.io.pd = pd;
    io_acb->u.io.which = which;
    io_acb->u.io.append = append_mode;
    io_acb->u.io.advance_fp = advance_fp;
    io_acb->u.io.vec = 0;
    io_acb->u.io.offset = offset;
    io_acb->u.io.bcnt = bcnt;
    io_acb->call_back_fn = cb;
    io_acb->call_back_dat = cdat;

    aiocommon_submit_op(io_acb);

    return 0;
}

/**
 * pxfs_rdwrv - implements pxfs read and write vector operations 
 */
static int pxfs_rdwrv(int fd,
                      const struct iovec *vector,
                      int count,
                      ssize_t *bcnt,
                      pxfs_cb cb,
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
    if (!pd || pd->is_in_use != PVFS_FS)
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
    io_acb->op_code = PVFS_AIO_IO_OP;
    io_acb->u.io.vector = (struct iovec *)vector;
    io_acb->u.io.count = count;
    io_acb->u.io.pd = pd;
    io_acb->u.io.which = which;
    io_acb->u.io.advance_fp = 1;
    io_acb->u.io.vec = 1;
    io_acb->u.io.offset = pd->s->file_pointer;
    io_acb->u.io.bcnt = bcnt;
    io_acb->call_back_fn = cb;
    io_acb->call_back_dat = cdat;

    aiocommon_submit_op(io_acb);

    return 0;
}

/**
 * pxfs_lseek
 */
extern int pxfs_lseek(int fd, off_t offset, int whence, off_t *offset_out,
                      pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *lseek_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    /* find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    lseek_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!lseek_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(lseek_acb, 0, sizeof(struct pvfs_aiocb));

    lseek_acb->hints = PVFS_HINT_NULL;
    lseek_acb->op_code = PVFS_AIO_LSEEK_OP;
    lseek_acb->u.lseek.pd = pd;
    lseek_acb->u.lseek.offset = (off64_t)offset;
    lseek_acb->u.lseek.whence = whence;
    lseek_acb->u.lseek.lseek64 = 0;
    lseek_acb->u.lseek.offset_out = (void *)offset_out;
    lseek_acb->call_back_fn = cb;
    lseek_acb->call_back_dat = cdat;

    aiocommon_submit_op(lseek_acb);

    return 0;
}

/**
 * pxfs_lseek64
 */
extern int pxfs_lseek64(int fd, off64_t offset, int whence, off64_t *offset_out,
                        pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *lseek_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    /* find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    lseek_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!lseek_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(lseek_acb, 0, sizeof(struct pvfs_aiocb));

    lseek_acb->hints = PVFS_HINT_NULL;
    lseek_acb->op_code = PVFS_AIO_LSEEK_OP;
    lseek_acb->u.lseek.pd = pd;
    lseek_acb->u.lseek.offset = offset;
    lseek_acb->u.lseek.whence = whence;
    lseek_acb->u.lseek.lseek64 = 1;
    lseek_acb->u.lseek.offset_out = (void *)offset_out;
    lseek_acb->call_back_fn = cb;
    lseek_acb->call_back_dat = cdat;

    aiocommon_submit_op(lseek_acb);

    return 0;
}

/**
 * pxfs datatypes and callback prototypes for truncate64 function
 */

typedef struct {
    int fd;
    off64_t length;
    int status;
    pxfs_cb cb;
    void *cdat;
} pxfs_trunc_dat;
    
static int pxfs_trunc_open_cb(void *cdat, int status);
static int pxfs_trunc_cb(void *cdat, int status);
static int pxfs_trunc_close_cb(void *cdat, int status);

static int pxfs_trunc_open_cb(void *cdat, int status)
{
    int rc;
    pxfs_trunc_dat *trunc = (pxfs_trunc_dat *)cdat;

    if (status)
    {
        (*trunc->cb)(trunc->cdat, status);
        free(trunc);
    }
    else
    {
        rc = pxfs_ftruncate64(trunc->fd, trunc->length, &pxfs_trunc_cb, trunc);
        if (rc < 0)
        {
            pxfs_trunc_cb(trunc, errno);
        }
    }

    return 0;
}

static int pxfs_trunc_cb(void *cdat, int status)
{
    int rc;
    pxfs_trunc_dat *trunc = (pxfs_trunc_dat *)cdat;

    if (status) trunc->status = status;
    else trunc->status = 0;

    rc = pxfs_close(trunc->fd, &pxfs_trunc_close_cb, trunc);
    if (rc < 0)
    {
        (*trunc->cb)(trunc->cdat, errno);
        free(trunc);
    }

    return 0;
}

static int pxfs_trunc_close_cb(void *cdat, int status)
{
    pxfs_trunc_dat *trunc = (pxfs_trunc_dat *)cdat;

    if (trunc->status)
    {
        (*trunc->cb)(trunc->cdat, trunc->status);
    }
    else
    {
        (*trunc->cb)(trunc->cdat, status);
    }
    free(trunc);
    return 0;
}

/*
 *
 **/

/**
 * pxfs_truncate
 */
extern int pxfs_truncate(const char *path, off_t length,
                         pxfs_cb cb, void *cdat)
{
    return pxfs_truncate64(path, (off64_t)length, cb, cdat);
}

/**
 * pxfs_truncate64
 */
extern int pxfs_truncate64(const char *path, off64_t length,
                           pxfs_cb cb, void *cdat)
{
    pxfs_trunc_dat *trunc = malloc(sizeof(pxfs_trunc_dat));
    if(!trunc)
    {
        errno = ENOMEM;
        return -1;
    }

    trunc->length = length;
    trunc->cb = cb;
    trunc->cdat = cdat;
    return pxfs_open(path, O_WRONLY, &trunc->fd, &pxfs_trunc_open_cb, trunc);
}

/**
 * pxfs_fallocate
 */
extern int pxfs_fallocate(int fd, off_t offset, off_t length,
                          pxfs_cb cb, void *cdat)
{
    if (offset < 0 || length < 0)
    {
        errno = EINVAL;
        return -1;
    }

    return pxfs_ftruncate64(fd, (off64_t)(offset) + (off64_t)(length), cb, cdat);
}

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
    if (!pd || pd->is_in_use != PVFS_FS)
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

/**
 * pxfs_close
 */
extern int pxfs_close(int fd, pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *close_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    close_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!close_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(close_acb, 0, sizeof(struct pvfs_aiocb));

    close_acb->hints = PVFS_HINT_NULL;
    close_acb->op_code = PVFS_AIO_CLOSE_OP;
    close_acb->u.close.pd = pd;
    close_acb->call_back_fn = cb;
    close_acb->call_back_dat = cdat;

    aiocommon_submit_op(close_acb);

    return 0;
}

//extern int pxfs_flush(int fd, pxfs_cb cb, void *cdat);  /* TODO: implemented? */

/**
 * pxfs datatypes and callbacks for stat(64) functions
 */

typedef struct {
    int fd;
    void *buf;
    uint32_t mask;
    int stat64;
    int status;
    pxfs_cb cb;
    void *cdat;
} pxfs_stat_dat;

static int pxfs_stat_open_cb(void *cdat, int status);
static int pxfs_stat_cb(void *cdat, int status);
static int pxfs_stat_close_cb(void *cdat, int status);

static int pxfs_stat_open_cb(void *cdat, int status)
{
    int rc;
    pxfs_stat_dat *stat = (pxfs_stat_dat *)cdat;

    if (status)
    {
        (*stat->cb)(stat->cdat, status);
        free(stat);
    }
    else
    {
        if (stat->stat64)
        {
            rc = pxfs_fstat64(stat->fd, (struct stat64 *)stat->buf,
                              &pxfs_stat_cb, stat);
        }
        else
        {
            rc = pxfs_fstat_mask(stat->fd, (struct stat *)stat->buf, 
                                 stat->mask, &pxfs_stat_cb, stat);
        }
        if (rc < 0)
        {
            pxfs_stat_cb(stat, errno);
        }
    }

    return 0;
}

static int pxfs_stat_cb(void *cdat, int status)
{
    int rc;
    pxfs_stat_dat *stat = (pxfs_stat_dat *)cdat;

    if (status) stat->status = status;
    else stat->status = 0;

    rc = pxfs_close(stat->fd, &pxfs_stat_close_cb, stat);
    if (rc < 0)
    {
        (*stat->cb)(stat->cdat, errno);
        free(stat);
    }

    return 0;
}

static int pxfs_stat_close_cb(void *cdat, int status)
{
    pxfs_stat_dat *stat = (pxfs_stat_dat *)cdat;

    if (stat->status)
    {
        (*stat->cb)(stat->cdat, stat->status);
    }
    else
    {
        (*stat->cb)(stat->cdat, status);
    }
    free(stat);
    return 0;
}

/*
 *
 **/

/**
 * pxfs_stat
 */
extern int pxfs_stat(const char *path, struct stat *buf,
                     pxfs_cb cb, void *cdat)
{
    return pxfs_stat_mask(path, buf, PVFS_ATTR_DEFAULT_MASK, cb, cdat);
}

/**
 * pxfs_stat64
 */
extern int pxfs_stat64(const char *path, struct stat64 *buf,
                       pxfs_cb cb, void *cdat)
{
    pxfs_stat_dat *stat = malloc(sizeof(pxfs_stat_dat));
    if (!stat)
    {
        errno = ENOMEM;
        return -1;
    }

    stat->buf = (void *)buf;
    stat->stat64 = 1;
    stat->cb = cb;
    stat->cdat = cdat;
    return pxfs_open(path, O_RDONLY, &stat->fd, &pxfs_stat_open_cb, stat);
}

/**
 * pxfs_stat_mask
 */
extern int pxfs_stat_mask(const char *path, struct stat *buf,
                          uint32_t mask, pxfs_cb cb, void *cdat)
{
    pxfs_stat_dat *stat = malloc(sizeof(pxfs_stat_dat));
    if (!stat)
    {
        errno = ENOMEM;
        return -1;
    }

    stat->buf = (void *)buf;
    stat->stat64 = 0;
    stat->mask = mask;
    stat->cb = cb;
    stat->cdat = cdat;
    return pxfs_open(path, O_RDONLY, &stat->fd, &pxfs_stat_open_cb, stat);
}

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
    stat_acb->op_code = PVFS_AIO_STAT_OP;
    stat_acb->u.stat.pd = pd;
    stat_acb->u.stat.buf = (void *)buf;
    stat_acb->u.stat.mask = PVFS_ATTR_DEFAULT_MASK;
    stat_acb->u.stat.stat64 = 1;
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
    stat_acb->u.stat.mask = mask & PVFS_ATTR_DEFAULT_MASK;
    stat_acb->u.stat.stat64 = 0;
    stat_acb->call_back_fn = cb;
    stat_acb->call_back_dat = cdat;

    aiocommon_submit_op(stat_acb);

    return 0;    
}

/**
 * pxfs_lstat
 */
extern int pxfs_lstat(const char *path, struct stat *buf,
                      pxfs_cb cb, void *cdat)
{
    return pxfs_lstat_mask(path, buf, PVFS_ATTR_DEFAULT_MASK, cb, cdat);
}

/**
 * pxfs_lstat64
 */
extern int pxfs_lstat64(const char *path, struct stat64 *buf,
                        pxfs_cb cb, void *cdat)
{
    pxfs_stat_dat *stat = malloc(sizeof(pxfs_stat_dat));
    if (!stat)
    {
        errno = ENOMEM;
        return -1;
    }

    stat->buf = (void *)buf;
    stat->stat64 = 1;
    stat->cb = cb;
    stat->cdat = cdat;
    return pxfs_open(path, O_RDONLY | O_NOFOLLOW, &stat->fd,
                     &pxfs_stat_open_cb, stat);
}

/**
 * pxfs_lstat_mask
 */
extern int pxfs_lstat_mask(const char *path, struct stat *buf, uint32_t mask,
                           pxfs_cb cb, void *cdat)
{
    pxfs_stat_dat *stat = malloc(sizeof(pxfs_stat_dat));
    if (!stat)
    {
        errno = ENOMEM;
        return -1;
    }

    stat->buf = (void *)buf;
    stat->stat64 = 0;
    stat->mask = mask;
    stat->cb = cb;
    stat->cdat = cdat;
    return pxfs_open(path, O_RDONLY | O_NOFOLLOW, &stat->fd,
                     &pxfs_stat_open_cb, stat);
}

/*

extern int pxfs_futimesat(int dirfd, const char *path,
                          const struct timeval times[2],
                          pxfs_cb cb, void *cdat);

extern int pxfs_utimes(const char *path, const struct timeval times[2],
                       pxfs_cb cb, void *cdat);

extern int pxfs_utime(const char *path, const struct utimbuf *buf,
                      pxfs_cb cb, void *cdat);

extern int pxfs_futimes(int fd, const struct timeval times[2],
                        pxfs_cb cb, void *cdat);

extern int pxfs_dup(int oldfd, int *newfd);

extern int pxfs_dup2(int oldfd, int newfd);

extern int pxfs_chown(const char *path, uid_t owner, gid_t group,
                      pxfs_cb cb, void *cdat);
*/

extern int pxfs_fchown(int fd, uid_t owner, gid_t group,
                       pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *chown_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    chown_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!chown_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(chown_acb, 0, sizeof(struct pvfs_aiocb));

    if (owner != -1)
    {
        chown_acb->u.chown.attr.owner = owner;
        chown_acb->u.chown.attr.mask |= PVFS_ATTR_SYS_UID;
    }
    if (group != -1)
    {
        chown_acb->u.chown.attr.group = group;
        chown_acb->u.chown.attr.mask |= PVFS_ATTR_SYS_GID;
    }

    chown_acb->hints = PVFS_HINT_NULL;
    chown_acb->op_code = PVFS_AIO_CHOWN_OP;
    chown_acb->u.chown.pd = pd;
    chown_acb->call_back_fn = cb;
    chown_acb->call_back_dat = cdat;

    aiocommon_submit_op(chown_acb);

    return 0;
}

/*
extern int pxfs_fchownat(int fd, const char *path, uid_t owner, gid_t group,
                         int flag, pxfs_cb, void *cdat);

extern int pxfs_lchown(const char *path, uid_t owner, gid_t group,
                       pxfs_cb cb, void *cdat);
*/

/**
 * pxfs datatypes and callbacks for chmod function
 */

typedef struct {
    int fd;
    mode_t mode;
    int status;
    pxfs_cb cb;
    void *cdat;
} pxfs_chmod_dat;

static int pxfs_chmod_open_cb(void *cdat, int status);
static int pxfs_chmod_cb(void *cdat, int status);
static int pxfs_chmod_close_cb(void *cdat, int status);

static int pxfs_chmod_open_cb(void *cdat, int status)
{
    int rc;
    pxfs_chmod_dat *chmod = (pxfs_chmod_dat *)cdat;

    if (status)
    {
        (*chmod->cb)(chmod->cdat, status);
        free(chmod);
    }
    else
    {
        rc = pxfs_fchmod(chmod->fd, chmod->mode, &pxfs_chmod_cb, chmod);
        if (rc < 0)
        {
            pxfs_chmod_cb(chmod, errno);
        }
    }

    return 0;
}

static int pxfs_chmod_cb(void *cdat, int status)
{
    int rc;
    pxfs_chmod_dat *chmod = (pxfs_chmod_dat *)cdat;

    if (status) chmod->status = status;
    else chmod->status = 0;

    rc = pxfs_close(chmod->fd, &pxfs_chmod_close_cb, chmod);
    if (rc < 0)
    {
        (*chmod->cb)(chmod->cdat, errno);
        free(chmod);
    }

    return 0;
}

static int pxfs_chmod_close_cb(void *cdat, int status)
{
    pxfs_chmod_dat *chmod = (pxfs_chmod_dat *)cdat;

    if (chmod->status)
    {
        (*chmod->cb)(chmod->cdat, chmod->status);
    }
    else
    {
        (*chmod->cb)(chmod->cdat, status);
    }
    free(chmod);
    return 0;
}

/*
 *
 **/

/**
 * pxfs_chmod
 */
extern int pxfs_chmod(const char *path, mode_t mode, pxfs_cb cb, void *cdat)
{
    pxfs_chmod_dat *chmod = malloc(sizeof(pxfs_chmod_dat));
    if (!chmod)
    {
        errno = ENOMEM;
        return -1;
    }

    chmod->mode = mode;
    chmod->cb = cb;
    chmod->cdat = cdat;
    return pxfs_open(path, O_RDONLY, &chmod->fd, &pxfs_chmod_open_cb, chmod);    
}

/**
 * pxfs_fchmod
 */
extern int pxfs_fchmod(int fd, mode_t mode, pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *chmod_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    chmod_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!chmod_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(chmod_acb, 0, sizeof(struct pvfs_aiocb));

    chmod_acb->hints = PVFS_HINT_NULL;
    chmod_acb->op_code = PVFS_AIO_CHMOD_OP;
    chmod_acb->u.chmod.pd = pd;
    chmod_acb->u.chmod.attr.perms = mode & 07777; /* mask off any stray bits */
    chmod_acb->u.chmod.attr.mask = PVFS_ATTR_SYS_PERM;
    chmod_acb->call_back_fn = cb;
    chmod_acb->call_back_dat = cdat;

    aiocommon_submit_op(chmod_acb);

    return 0;
}

/*
extern int pxfs_fchmodat(int fd, const char *path, mode_t mode, int flag,
                         pxfs_cb cb, void *cdat);
*/

/**
 * pxfs_mkdir
 */
extern int pxfs_mkdir(const char *path, mode_t mode, pxfs_cb cb, void *cdat)
{
    char *newpath;
    int rc;
    struct pvfs_aiocb *mkdir_acb = NULL;

    if (!path)
    {
        errno = EINVAL;
        return -1;
    }

    mkdir_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!mkdir_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(mkdir_acb, 0, sizeof(struct pvfs_aiocb));

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }

    /* strip any trailing slashes */
    int tmp_len = strlen(newpath);
    while (tmp_len > 1 && newpath[tmp_len - 1] == '/')
    {
        newpath[tmp_len - 1] = '\0';
        tmp_len--;
    }

    rc = split_pathname(newpath, 1, &mkdir_acb->u.mkdir.directory,
                        &mkdir_acb->u.mkdir.filename);
    if (rc < 0)
    {
        return -1;
    }

    mkdir_acb->hints = PVFS_HINT_NULL;
    mkdir_acb->op_code = PVFS_AIO_MKDIR_OP;
    mkdir_acb->u.mkdir.mode = (mode & ~mask_val & 0777);
    mkdir_acb->u.mkdir.pdir = NULL;
    mkdir_acb->call_back_fn = cb;
    mkdir_acb->call_back_dat = cdat;

    aiocommon_submit_op(mkdir_acb);

    if (newpath != path)
    {
        free(newpath);
    }
    
    return 0;
}

/*
extern int pxfs_mkdirat(int dirfd, const char *path, mode_t mode,
                        pxfs_cb cb, void *cdat);
*/

/**
 * pxfs_rmdir
 */
extern int pxfs_rmdir(const char *path, pxfs_cb cb, void *cdat)
{
    char *newpath;
    int rc;
    struct pvfs_aiocb *rmdir_acb = NULL;

    if (!path)
    {
        errno = EINVAL;
        return -1;
    }

    rmdir_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!rmdir_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(rmdir_acb, 0, sizeof(struct pvfs_aiocb));

    newpath = pvfs_qualify_path(path);
    if (!newpath)
    {
        return -1;
    }

    /* strip any trailing slashes */
    int tmp_len = strlen(newpath);
    while (tmp_len > 1 && newpath[tmp_len - 1] == '/')
    {
        newpath[tmp_len - 1] = '\0';
        tmp_len--;
    }

    rc = split_pathname(newpath, 1, &rmdir_acb->u.remove.directory,
                        &rmdir_acb->u.remove.filename);
    if (rc < 0)
    {
        return -1;
    }

    rmdir_acb->hints = PVFS_HINT_NULL;
    rmdir_acb->op_code = PVFS_AIO_REMOVE_OP;
    rmdir_acb->u.remove.pdir = NULL;
    rmdir_acb->u.remove.dirflag = 1;
    rmdir_acb->call_back_fn = cb;
    rmdir_acb->call_back_dat = cdat;

    aiocommon_submit_op(rmdir_acb);

    if (newpath != path)
    {
        free(newpath);
    }

    return 0;
}

/**
 * pxfs datatypes and callbacks for readlink function
 */

typedef struct {
    int fd;
    char *buf;
    size_t bufsiz;
    ssize_t *bcnt;
    int status;
    pxfs_cb cb;
    void *cdat;
} pxfs_readlink_dat;

static int pxfs_readlink_open_cb(void *cdat, int status);
static int pxfs_readlink_cb(void *cdat, int status);
static int pxfs_readlink_close_cb(void *cdat, int status);

static int pxfs_readlink_open_cb(void *cdat, int status)
{
    int rc = 0;
    pxfs_readlink_dat *readlink = (pxfs_readlink_dat *)cdat;
    pvfs_descriptor *pd;

    if (status)
    {
        (*readlink->cb)(readlink->cdat, status);
        free(readlink);
    }
    else
    {
        /* sumbit a readlink aio op */
        if (!readlink->buf)
        {
            errno = EINVAL;
            rc = -1;
            goto errorout;
        }
        if (readlink->fd < 0)
        {
            errno = EBADF;
            rc = -1;
            goto errorout;
        }
        pd = pvfs_find_descriptor(readlink->fd);
        if (!pd || pd->is_in_use != PVFS_FS)
        {
            errno = EBADF;
            rc = -1;
            goto errorout;
        }

        struct pvfs_aiocb *readlink_acb = malloc(sizeof(struct pvfs_aiocb));
        if (!readlink_acb)
        {
            errno = ENOMEM;
            rc = -1;
            goto errorout;
        }
        memset(readlink_acb, 0, sizeof(struct pvfs_aiocb));

        readlink_acb->hints = PVFS_HINT_NULL;
        readlink_acb->op_code = PVFS_AIO_READLINK_OP;
        readlink_acb->u.readlink.pd = pd;
        readlink_acb->u.readlink.buf = readlink->buf;
        readlink_acb->u.readlink.bufsiz = readlink->bufsiz;
        readlink_acb->u.readlink.bcnt = readlink->bcnt;
        readlink_acb->call_back_fn = &pxfs_readlink_cb;
        readlink_acb->call_back_dat = readlink;

        aiocommon_submit_op(readlink_acb);

errorout:
        if (rc < 0)
        {
            pxfs_readlink_cb(readlink, errno);
        }
    }

    return 0;
}

static int pxfs_readlink_cb(void *cdat, int status)
{
    int rc;
    pxfs_readlink_dat *readlink = (pxfs_readlink_dat *)cdat;

    if (status) readlink->status = status;
    else readlink->status = 0;

    rc = pxfs_close(readlink->fd, &pxfs_readlink_close_cb, readlink);
    if (rc < 0)
    {
        (*readlink->cb)(readlink->cdat, errno);
        free(readlink);
    }

    return 0;
}

static int pxfs_readlink_close_cb(void *cdat, int status)
{
    pxfs_readlink_dat *readlink = (pxfs_readlink_dat *)cdat;

    if (readlink->status)
    {
        (*readlink->cb)(readlink->cdat, readlink->status);
    }
    else
    {
        (*readlink->cb)(readlink->cdat, status);
    }
    free(readlink);
    return 0;
}

/*
 *
 **/

/**
 * pxfs_readlink
 */
extern int pxfs_readlink(const char *path, char *buf, size_t bufsiz,
                         ssize_t *bcnt, pxfs_cb cb, void *cdat)
{
    pxfs_readlink_dat *readlink = malloc(sizeof(pxfs_readlink_dat));
    if (!readlink)
    {
        errno = ENOMEM;
        return -1;
    }

    readlink->buf = buf;
    readlink->bufsiz = bufsiz;
    readlink->bcnt = bcnt;
    readlink->cb = cb;
    readlink->cdat = cdat;
    return pxfs_open(path, O_RDONLY | O_NOFOLLOW, &readlink->fd,
                     &pxfs_readlink_open_cb, readlink);
}

/*
extern int pxfs_readlinkat(int dirfd, const char *path, char *buf,
                           size_t bufsiz, ssize_t *bcnt,
                           pxfs_cb cb, void *cdat);
*/

/**
 * pxfs_symlink
 */
extern int pxfs_symlink(const char *oldpath, const char *newpath,
                        pxfs_cb cb, void *cdat)
{
    int rc;
    char *abspath;
    struct pvfs_aiocb *symlink_acb = NULL;

    if (!oldpath || !newpath)
    {
        errno = EINVAL;
        return -1;
    }

    symlink_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!symlink_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(symlink_acb, 0, sizeof(struct pvfs_aiocb));  
    
    abspath = pvfs_qualify_path(newpath);
    if (!abspath)
    {
        return -1;
    }

    rc = split_pathname(abspath, 0, &(symlink_acb->u.symlink.new_directory),
                        &(symlink_acb->u.symlink.new_filename));
    if (rc < 0)
    {
        return -1;
    }

    symlink_acb->hints = PVFS_HINT_NULL;
    symlink_acb->op_code = PVFS_AIO_SYMLINK_OP;
    symlink_acb->u.symlink.link_target = oldpath;
    symlink_acb->u.symlink.pdir = NULL;
    symlink_acb->call_back_fn = cb;
    symlink_acb->call_back_dat = cdat;

    aiocommon_submit_op(symlink_acb);

    if (abspath != newpath)
    {
        free(abspath);
    }

    return 0;
}

/*
extern int pxfs_symlinkat(const char *oldpath, int newdirfd,
                          const char *newpath, pxfs_cb cb, void *cdat);
*/

/**
 * PVFS does not have hard links
 */
extern int pxfs_link(const char *oldpath, const char *newpath,
                     pxfs_cb cb, void *cdat)
{
    fprintf(stderr, "pxfs_link not implemented\n");
    errno = ENOSYS;
    return -1;
}

/**
 * PVFS does not have hard links
 */
extern int pxfs_linkat(int olddirfd, const char *oldpath,
                       int newdirfd, const char *newpath, int flags,
                       pxfs_cb cb, void *cdat)
{
    fprintf(stderr, "pxfs_linkat not implemented\n");
    errno = ENOSYS;
    return -1;
}

/*
extern int pxfs_readdir(unsigned int fd, struct dirent *dirp,
                        unsigned int count, pxfs_cb, void *cdat);

extern int pxfs_getdents(unsigned int fd, struct dirent *dirp,
                         unsigned int count, pxfs_cb, void *cdat);

extern int pxfs_getdents64(unsigned int fd, struct dirent64 *dirp,
                           unsigned int count, pxfs_cb, void *cdat);

extern int pxfs_access(const char *path, int mode, pxfs_cb, void *cdat);

extern int pxfs_faccessat(int dirfd, const char *path, int mode, int flags,
                          pxfs_cb cb, void *cdat);
*/

/*
 * PVFS does not implement flocks
 */
extern int pxfs_flock(int fd, int op, pxfs_cb cb, void *cdat)
{
    errno = ENOSYS;
    fprintf(stderr, "pxfs_flock not implemented\n");
    return -1;
}

/*
extern int pxfs_fcntl(int fd, int cmd, pxfs_cb, void *cdat, ...);

extern int pxfs_sync(pxfs_cb, void *cdat);
*/

/**
 * pxfs_fsync
 */
extern int pxfs_fsync(int fd, pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd;
    struct pvfs_aiocb *fsync_acb = NULL;

    if (fd < 0)
    {
        errno = EBADF;
        return -1;
    }
    /* Find the descriptor */
    pd = pvfs_find_descriptor(fd);
    if (!pd || pd->is_in_use != PVFS_FS)
    {
        errno = EBADF;
        return -1;
    }

    fsync_acb = malloc(sizeof(struct pvfs_aiocb));
    if (!fsync_acb)
    {
        errno = ENOMEM;
        return -1;
    }
    memset(fsync_acb, 0, sizeof(struct pvfs_aiocb));

    fsync_acb->hints = PVFS_HINT_NULL;
    fsync_acb->op_code = PVFS_AIO_FSYNC_OP;
    fsync_acb->u.fsync.pd = pd;
    fsync_acb->call_back_fn = cb;
    fsync_acb->call_back_dat = cdat;
    
    aiocommon_submit_op(fsync_acb);

    return 0;
}

/*
extern int pxfs_fdatasync(int fd, pxfs_cb cb, void *cdat);
*/

/*
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
*/
/*
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

extern int pxfs_chdir(const char *path);

extern int pxfs_fchdir(int fd);

extern int pxfs_cwd_init(const char *buf, size_t size);

extern char *pxfs_getcwd(char *buf, size_t size);

extern char *pxfs_get_current_dir_name(void);

extern char *pxfs_getwd(char *buf);

extern mode_t pxfs_umask(mode_t mask);

extern mode_t pxfs_getumask(void);

extern int pxfs_getdtablesize(void);
*/

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
