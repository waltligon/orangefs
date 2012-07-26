/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#include "aio-pvfs.h"
#include "aiocommon.h"
#include "pvfs2-aio.h"

/* TODO: Implement this 
int pvfs_aio_cancel(int fd, struct aiocb *aiocbp)
{
}
*/


int pvfs_aio_error(const struct aiocb *aiocbp)
{
   if (!aiocbp)
   {
      errno = EINVAL;
      return -1;
   }

   /* return the error code */
   return aiocbp->__error_code;
}

//int pvfs_aio_fsync(int op, struct aiocb *aiocbp);

int pvfs_aio_read(struct aiocb *aiocbp)
{
   if (!aiocbp)
   {
      errno = EINVAL;
      return -1;
   }
   aiocbp->aio_lio_opcode = LIO_READ;

   return pvfs_lio_listio(LIO_NOWAIT, &aiocbp, 1, NULL);
}

ssize_t pvfs_aio_return(struct aiocb *aiocbp)
{
   /* if the aiocbp is invalid or the cb is still in progress, return an error */
   if (!aiocbp || (aiocbp->__error_code == EINPROGRESS))
   {
      errno = EINVAL;
      return -1;
   }

   /* return the aio return value */
   return aiocbp->__return_value;
}

//int pvfs_aio_suspend(const struct aiocb * const cblist[], int n,
//                        const struct timespec *timeout);

int pvfs_aio_write(struct aiocb *aiocbp)
{
   if (!aiocbp)
   {
      errno = EINVAL;
      return -1;
   }
   aiocbp->aio_lio_opcode = LIO_WRITE;

   return pvfs_lio_listio(LIO_NOWAIT, &aiocbp, 1, NULL);
}

int pvfs_lio_listio(int mode, struct aiocb * const list[], int nent,
                    struct sigevent *sig)
{
    int i;
    pvfs_descriptor *pd = NULL;
    struct pvfs_aiocb **pvfs_list;
 
    /* TODO: HANDLE sig */
    /* TODO: handle the mode argument, i.e. implement a wait function and call */

    if (nent > PVFS_AIO_LISTIO_MAX || (mode != LIO_WAIT && mode != LIO_NOWAIT) ||
        !list)
    {
        errno = EINVAL;
        return -1;
    } 
   
    pvfs_list = malloc(nent * sizeof(struct pvfs_aiocb *));
    if (pvfs_list == NULL)
    {
        errno = ENOMEM;
        return -1;
    }

    for (i = 0; i < nent; i++)
    {
        /* if the control block is a NULL pointer, then ignore it */
        if (list[i] == NULL)
        {
            nent--;
            i--;
            continue;
        }

        pvfs_list[i] = malloc(sizeof(struct pvfs_aiocb));
        if (pvfs_list[i] == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
        memset(pvfs_list[i], 0, sizeof(struct pvfs_aiocb));

        if (list[i]->aio_fildes < 0)
        {
            list[i]->__error_code = EBADF;
            list[i]->__return_value = -1;
        }
        
        if (!(list[i]->aio_buf))
        {
            list[i]->__error_code = EINVAL;
            list[i]->__return_value = -1;
        }

        pd = pvfs_find_descriptor(list[i]->aio_fildes);
        if (!pd)
        {
            list[i]->__error_code = EBADF;
            list[i]->__error_code = -1;
        }

        list[i]->__error_code = EINPROGRESS;
        pvfs_list[i]->hints = PVFS_HINT_NULL;
        pvfs_list[i]->op_code = PVFS_AIO_IO_OP;
        pvfs_list[i]->u.io.vector.iov_len = list[i]->aio_nbytes;
        pvfs_list[i]->u.io.vector.iov_base = (void *)list[i]->aio_buf;
        pvfs_list[i]->u.io.pd = pd;
        pvfs_list[i]->u.io.offset = list[i]->aio_offset;
        pvfs_list[i]->u.io.bcnt = &(list[i]->__return_value);
        pvfs_list[i]->call_back_fn = &pvfs_aio_lio_callback;
        pvfs_list[i]->call_back_dat = list[i];
        switch (list[i]->aio_lio_opcode)
        {
            case LIO_READ:
                pvfs_list[i]->u.io.which = PVFS_IO_READ;
                aiocommon_submit_op(pvfs_list[i]);
                break;
            case LIO_WRITE:
                pvfs_list[i]->u.io.which = PVFS_IO_WRITE;
                aiocommon_submit_op(pvfs_list[i]);
                break;
            case LIO_NOP:
                list[i]->__error_code = 0;
                list[i]->__return_value = 0;
                break;
        }
    }

    return 0;
}

/* call back function for aio lio operation */
int pvfs_aio_lio_callback(void *cdat, int status)
{
    struct aiocb *finished_cb = (struct aiocb *)cdat;

    finished_cb->__error_code = status;

    return 1;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
