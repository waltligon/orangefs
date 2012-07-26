/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include "aio-pvfs.h"
#include "aiocommon.h"

/* TODO: Implement this 
int pvfs_aio_cancel(int fd, struct aiocb *aiocbp)
{
}
*/


int pvfs_aio_error(const struct aiocb *aiocbp)
{
   /* verify aiocbp and make sure the pvfs aiocb still exists (stored at __next_prio) */
   if (!aiocbp || !aiocbp->__next_prio)
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
      errno = EFAULT;
      return -1;
   }
   aiocbp->aio_lio_opcode = LIO_READ;

   return pvfs_lio_listio(LIO_NOWAIT, &aiocbp, 1, NULL);
}

ssize_t pvfs_aio_return(struct aiocb *aiocbp)
{
   /* if the aiocbp is invalid or the cb is still in progress, return an error */
   if (!aiocbp || !aiocbp->__next_prio || (aiocbp->__error_code == EINPROGRESS))
   {
      errno = EINVAL;
      return -1;
   }

   /* remove the pvfs aiocb from internal structures (this will make future calls
    * to return() or error() fail) */
   aiocommon_remove_cb((struct pvfs_aiocb *)aiocbp->__next_prio);
   free((struct pvfs_aiocb *)aiocbp->__next_prio);
   aiocbp->__next_prio = NULL;

   gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p removed\n", aiocbp);

   /* return the aio return value */
   return aiocbp->__return_value;
}

//int pvfs_aio_suspend(const struct aiocb * const cblist[], int n,
//                        const struct timespec *timeout);

int pvfs_aio_write(struct aiocb *aiocbp)
{
   if (!aiocbp)
   {
      errno = EFAULT;
      return -1;
   }
   aiocbp->aio_lio_opcode = LIO_WRITE;

   return pvfs_lio_listio(LIO_NOWAIT, &aiocbp, 1, NULL);
}

int pvfs_lio_listio(int mode, struct aiocb * const list[], int nent,
                    struct sigevent *sig)
{
   int i;
   struct pvfs_aiocb **pvfs_list;   
 
   /* TODO: HANDLE sig */
   /* TODO: handle the mode argument, i.e. implement a wait function and call */

   if (nent > PVFS_AIO_LISTIO_MAX || (mode != LIO_WAIT && mode != LIO_NOWAIT))
   {
      errno = EINVAL;
      return -1;
   } 
   
   if (list == NULL)
   {
      errno = EFAULT;
      return -1;
   }

   pvfs_list = (struct pvfs_aiocb **)malloc(nent * sizeof(struct pvfs_aiocb *));
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

      pvfs_list[i] = (struct pvfs_aiocb *)malloc(sizeof(struct pvfs_aiocb));
      if (pvfs_list[i] == NULL)
      {
         errno = ENOMEM;
         return -1;
      }

      /* make the aiocb and pvfscb point to each other */
      pvfs_list[i]->a_cb = list[i];
      list[i]->__next_prio = (void *)pvfs_list[i];
   }

   return aiocommon_lio_listio(pvfs_list, nent);
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
