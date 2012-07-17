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
    int rc;
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

    rc = iocommon_cred(&open_acb->cred_p);
    if (rc < 0)
    {
        free(open_acb);
        return -1;
    }

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
    open_acb->u.open.fd = fd;
    open_acb->call_back_fn = cb;
    open_acb->call_back_dat = cdat;

    aiocommon_submit_op(open_acb);

    return 0;
}

extern int pxfs_read(int fd, void *buf, size_t count, ssize_t *bcnt,
                     pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd = NULL;
    struct iovec vector;
    int rc; 
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

    rc = iocommon_cred(&read_acb->cred_p);
    if (rc < 0)
    {
        free(read_acb);
        return -1;
    }

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        free(read_acb);
        return -1;
    }    

    rc = PVFS_Request_contiguous(count, PVFS_BYTE,
                                 &(read_acb->u.io.file_req));
    if (rc < 0)
    {
        free(read_acb);
        return -1;
    }

    vector.iov_len = count;
    vector.iov_base = (void *)buf;

    rc = pvfs_convert_iovec(&vector, 1, &(read_acb->u.io.mem_req),
                            &(read_acb->u.io.buf));
    if (rc < 0)
    {
        free(read_acb);
        return -1;
    }

    read_acb->hints = PVFS_HINT_NULL;
    read_acb->op_code = PVFS_AIO_IO_OP;
    read_acb->u.io.pd = pd;
    read_acb->u.io.which = PVFS_IO_READ;
    read_acb->u.io.offset = pd->s->file_pointer;
    read_acb->u.io.bcnt = bcnt;
    read_acb->call_back_fn = cb;
    read_acb->call_back_dat = cdat;

    aiocommon_submit_op(read_acb);

    return 0;
}

extern int pxfs_write(int fd, const void *buf, size_t count, ssize_t *bcnt,
                      pxfs_cb cb, void *cdat)
{
    pvfs_descriptor *pd = NULL;
    struct iovec vector;
    int rc;
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

    rc = iocommon_cred(&write_acb->cred_p);
    if (rc < 0)
    {
        free(write_acb);
        return -1;
    }

    pd = pvfs_find_descriptor(fd);
    if (!pd)
    {
        free(write_acb);
        return -1;
    }

    rc = PVFS_Request_contiguous(count, PVFS_BYTE,
                                 &(write_acb->u.io.file_req));
    if (rc < 0)
    {
        free(write_acb);
        return -1;
    }

    vector.iov_len = count;
    vector.iov_base = (void *)buf;

    rc = pvfs_convert_iovec(&vector, 1, &(write_acb->u.io.mem_req),
                            &(write_acb->u.io.buf));
    if (rc < 0)
    {
        free(write_acb);
        return -1;
    }

    write_acb->hints = PVFS_HINT_NULL;
    write_acb->op_code = PVFS_AIO_IO_OP;
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
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
