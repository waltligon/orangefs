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

extern int pxfs_open(const char *path, int flags, int *fd,
                     pxfs_cb cb, void *cdat, ...)
{
    va_list ap;
    int mode;
    PVFS_hint hints;
    PVFS_credential *credential;
    int rc;
    struct pvfs_aiocb *open_acb;

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

    rc = aiocommon_submit_op(open_acb);
    if (rc < 0)
    {
        AIO_SET_ERR(-(open_acb->error_code));
    }
    return rc;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
