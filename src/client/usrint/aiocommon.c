/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include "usrint.h"
#include "posix-ops.h"
#include "openfile-util.h"
#include "iocommon.h"
#include "aiocommon.h"

/* prototypes */
static int aiocommon_readorwrite(struct pvfs_aiocb *p_cb);
static void *aiocommon_progress(void *ptr);

/* linked list variables used to implement aio structures */
static struct qlist_head *aio_waiting_list = NULL;
static struct qlist_head *aio_running_list = NULL;
static struct qlist_head *aio_finished_list = NULL;
static gen_mutex_t waiting_list_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t running_list_mutex = GEN_MUTEX_INITIALIZER;
static gen_mutex_t finished_list_mutex = GEN_MUTEX_INITIALIZER;

/* PROGRESS THREAD VARIABLES */
static pthread_t aio_progress_thread;
static gen_mutex_t progress_sync_mutex = GEN_MUTEX_INITIALIZER;
static int progress_running = PVFS_AIO_PROGRESS_IDLE;
static int num_aiocbs_running = 0;
static PVFS_sys_op_id running_ops[PVFS_AIO_MAX_RUNNING] = {0};

/* Initialization of PVFS AIO system */
int aiocommon_init()
{
   /* initialize waiting, running, and finished lists for aio implementation */
   aio_waiting_list = (struct qlist_head *)malloc(sizeof(struct qlist_head));
   if (aio_waiting_list == NULL)
   {
      return -1;
   }
   INIT_QLIST_HEAD(aio_waiting_list);

   aio_running_list = (struct qlist_head *)malloc(sizeof(struct qlist_head));
   if (aio_running_list == NULL)
   {
      return -1;
   }
   INIT_QLIST_HEAD(aio_running_list);

   aio_finished_list = (struct qlist_head *)malloc(sizeof(struct qlist_head));
   if (aio_finished_list == NULL)
   {
      return -1;
   }
   INIT_QLIST_HEAD(aio_finished_list);
   
   gossip_debug(GOSSIP_USRINT_DEBUG, "Successfully initalized PVFS AIO inteface\n");

   return 0;
}

/* IMPLEMENTATION OF LOW LEVEL PVFS AIO ROUTINES */
int aiocommon_lio_listio(struct pvfs_aiocb *list[],
	        	 int nent)
{
   int i;
   int ret = 0;

   /* make sure the library has been initialized for this process */
   pvfs_sys_init();

   /* check arguments */
   if (list == NULL || nent < 1 || nent > PVFS_AIO_LISTIO_MAX)
   {
      errno = EINVAL;
      return -1;
   }

   /* verify AIO structures are initalized properly */
   if (!aio_waiting_list || !aio_running_list || !aio_finished_list)
   {
      errno = EFAULT;
      return -1;
   }

   gen_mutex_lock(&progress_sync_mutex);
   for (i = 0; i < nent; i++)
   {
      assert(list[i]);

      if (num_aiocbs_running < PVFS_AIO_MAX_RUNNING)
      {
         /* submit the request */
         ret = aiocommon_readorwrite(list[i]);

         /* if the request failed or completed immediately, add to the finished list */
         if (ret < 1)
         {
            gen_mutex_lock(&finished_list_mutex);
            qlist_add_tail(&(list[i]->link), aio_finished_list);
            gen_mutex_unlock(&finished_list_mutex);
         }

         /* else the request deferred completion, and is added to the running list */
         else
         {
            gen_mutex_lock(&running_list_mutex);
            qlist_add_tail(&(list[i]->link), aio_running_list);
            gen_mutex_unlock(&running_list_mutex);

            running_ops[num_aiocbs_running++] = list[i]->op_id;
            if (progress_running == PVFS_AIO_PROGRESS_IDLE)
            {
               pthread_create(&aio_progress_thread, NULL, aiocommon_progress, NULL);
               progress_running = PVFS_AIO_PROGRESS_RUNNING;
            }
         }
      }
      else
      {
          gossip_debug(GOSSIP_USRINT_DEBUG, "%d AIO requests already posted,"
                       "AIO CB %p added to AIO waiting list\n",
                       num_aiocbs_running, list[i]->a_cb);
          /* add the pvfs_cb to the waiting list, the running list is full */
          gen_mutex_lock(&waiting_list_mutex);
          qlist_add_tail(&(list[i]->link), aio_waiting_list);
          list[i]->a_cb->__error_code = EINPROGRESS;
          gossip_debug(GOSSIP_USRINT_DEBUG, "%d AIO requests now waiting\n", 
                       qlist_count(aio_waiting_list));
          gen_mutex_unlock(&waiting_list_mutex);
      }
   }

   gen_mutex_unlock(&progress_sync_mutex);
   return 0;
}

/* returns 0 on immediate completion, 1 on deferred completion, -1 on error */
static int aiocommon_readorwrite(struct pvfs_aiocb *p_cb)
{
   enum PVFS_io_type which;
   PVFS_credential *creds;
   pvfs_descriptor *pd;
   struct iovec vector;
   void *buf;
   int orig_errno = errno;
   int rc = 0;

   iocommon_cred(&creds);

   pd = pvfs_find_descriptor(p_cb->a_cb->aio_fildes);
   if (!pd || pd->is_in_use != PVFS_FS)
   {
      p_cb->a_cb->__error_code = EBADF;
      p_cb->a_cb->__return_value = -1;
      return -1;
   }

   memset(&(p_cb->io_resp), 0, sizeof(p_cb->io_resp));

   rc = PVFS_Request_contiguous(p_cb->a_cb->aio_nbytes,
	                        PVFS_BYTE,
	                        &(p_cb->file_req));
   IOCOMMON_RETURN_ERR(rc);

   vector.iov_len = p_cb->a_cb->aio_nbytes;
   vector.iov_base = (void *)p_cb->a_cb->aio_buf;

   rc = pvfs_convert_iovec(&vector, 1, &(p_cb->mem_req), &buf);
   IOCOMMON_RETURN_ERR(rc);

   /* handle opcode */
   switch(p_cb->a_cb->aio_lio_opcode)
   {
      case LIO_READ:
         gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p attempting to read %d bytes\n", p_cb->a_cb, (int)p_cb->a_cb->aio_nbytes);
         which = PVFS_IO_READ;
         break;
      case LIO_WRITE:
         gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p attempting to write %d bytes\n", p_cb->a_cb, (int)p_cb->a_cb->aio_nbytes);
         which = PVFS_IO_WRITE;
         break;
      case LIO_NOP:
         gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p, NOP\n", p_cb->a_cb);
         p_cb->a_cb->__error_code = 0;
         p_cb->a_cb->__return_value = 0;
         return 0;
      default:
	p_cb->a_cb->__error_code = EINVAL;
	p_cb->a_cb->__return_value = -1;
        return -1;
   }

   /* make asynchronous io call to the file system */
   rc = PVFS_isys_io(pd->s->pvfs_ref,
                     p_cb->file_req,
                     p_cb->a_cb->aio_offset,
                     buf,
                     p_cb->mem_req,
                     creds,
                     &(p_cb->io_resp),
                     which,
                     &(p_cb->op_id),
                     PVFS_HINT_NULL,
                     (void *)p_cb);
   IOCOMMON_CHECK_ERR(rc);

   /* if this pvfs_cb failed set the error and return value */
   if (rc < 0)
   {
      gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p, FAILED with error %d\n",
                   p_cb->a_cb, errno);
      p_cb->a_cb->__error_code = errno;
      p_cb->a_cb->__return_value = -1;
   }

   /* else the io operation completed immediately */
   else if (rc == 0 && p_cb->op_id == -1)
   {
      gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p, COMPLETED immediately (%d bytes)\n",
                    p_cb->a_cb, (int)p_cb->io_resp.total_completed);
      p_cb->a_cb->__error_code = 0;
      p_cb->a_cb->__return_value = p_cb->io_resp.total_completed;
   }

   /* else, the io operation deferred completion */
   else
   {
      gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p, DEFERRED\n", p_cb->a_cb);
      p_cb->a_cb->__error_code = EINPROGRESS;
      rc = 1;
   }

errorout:
   return rc;
}

static void *aiocommon_progress(void *ptr)
{
   int i, j;
   int ret = 0;
   int op_count = 0;
   struct qlist_head *next_io;
   struct pvfs_aiocb *io_cb;
   PVFS_sys_op_id ret_op_ids[PVFS_AIO_MAX_RUNNING];
   PVFS_sys_op_id temp_running_ops[PVFS_AIO_MAX_RUNNING] = {0};
   int err_code_array[PVFS_AIO_MAX_RUNNING] = {0};
   struct pvfs_aiocb *aiocb_array[PVFS_AIO_MAX_RUNNING] = {NULL};

   gossip_debug(GOSSIP_USRINT_DEBUG, "AIO progress thread starting up\n");

   /* progress thread */
   while (1)
   {
      /* fill the running list before forcing progress */
      gen_mutex_lock(&progress_sync_mutex);
      while (1)
      {
         /* stop adding cb's if the running list is full or the waiting list is empty */
         gen_mutex_lock(&waiting_list_mutex);
         if ((num_aiocbs_running == PVFS_AIO_MAX_RUNNING) || qlist_empty(aio_waiting_list))
         {
            gen_mutex_unlock(&waiting_list_mutex);
            break;
         }

         /* get first item off the waiting list */
         next_io = qlist_pop(aio_waiting_list);
         io_cb = qlist_entry(next_io, struct pvfs_aiocb, link);
         gen_mutex_unlock(&waiting_list_mutex);

         gossip_debug(GOSSIP_USRINT_DEBUG, "Adding AIO CB %p to the running list\n", io_cb->a_cb);

         /* submit the request */
         ret = aiocommon_readorwrite(io_cb);

         /* if the request failed or completed immediately, add to the finished list */
         if (ret < 1)
         {
            gen_mutex_lock(&finished_list_mutex);
            qlist_add_tail(&(io_cb->link), aio_finished_list);
            gen_mutex_unlock(&finished_list_mutex);
         }

         /* else the request deferred completion, and is added to the running list */
         else
         {
            gen_mutex_lock(&running_list_mutex);
            qlist_add_tail(&(io_cb->link), aio_running_list);
            gen_mutex_unlock(&running_list_mutex);

            running_ops[num_aiocbs_running++] = io_cb->op_id;
         }
      }

      /* call PVFS_sys_testsome() to force progress on "running" operations in the system.
       * NOTE: the op_ids of the completed ops will be in ret_op_ids, the number of operations
       *       will be in op_count, the user pointers (pvfs_aiocb structure pointers) are stored in
       *       aiocb_array, and the error codes are stored in err_code array.
       */
      memcpy(ret_op_ids, running_ops, (num_aiocbs_running * sizeof(PVFS_sys_op_id)));
      op_count = num_aiocbs_running;
      ret = PVFS_sys_testsome(ret_op_ids,
                              &op_count,
                              (void *)aiocb_array,
                              err_code_array,
                              PVFS_AIO_DEFAULT_TIMEOUT_MS);

      /* for each op returned */
      for (i = 0; i < op_count; i++)
      {
         /* ignore completed items that do not have a user pointer (these are not aiocbs)*/
         if (aiocb_array[i] == NULL) continue;

         /* remove the cb from the running list */
         gen_mutex_lock(&running_list_mutex);
         qlist_del(&(aiocb_array[i]->link));
         gen_mutex_unlock(&running_list_mutex);

         /* move the cb to the finished list */
         gen_mutex_lock(&finished_list_mutex);
         qlist_add_tail(&(aiocb_array[i]->link), aio_finished_list);
         gen_mutex_unlock(&finished_list_mutex);

         /* if the operation had no error */
         if (!err_code_array[i])
         {
            gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p COMPLETED (%d bytes)\n",
                         aiocb_array[i]->a_cb, (int)aiocb_array[i]->io_resp.total_completed);
            aiocb_array[i]->a_cb->__error_code = 0;
            aiocb_array[i]->a_cb->__return_value = aiocb_array[i]->io_resp.total_completed;
         }
         /* else the operation failed */
         else
         {
            /* map the PVFS sysint error to a POSIX errno */
            if (IS_PVFS_NON_ERRNO_ERROR(-(err_code_array[i])))
            {
               aiocb_array[i]->a_cb->__error_code = EIO;
            }

            else if (IS_PVFS_ERROR(-(err_code_array[i])))
            {
               aiocb_array[i]->a_cb->__error_code = PINT_errno_mapping[(-(err_code_array[i])) & 0x7f];
            }

            gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p FAILED with error %d\n",
                         aiocb_array[i]->a_cb, aiocb_array[i]->a_cb->__error_code);
            aiocb_array[i]->a_cb->__return_value = -1;
         }

         /* update the number of running cbs, and exit the thread if this number is 0 */
         for (j = 0; j < num_aiocbs_running; j++)
         {
            if (running_ops[j] == aiocb_array[i]->op_id)
            {
               /* remove it from the running op_ids array */
               if (j > 0) memcpy(temp_running_ops, running_ops, j * sizeof(PVFS_sys_op_id));
               if (j < num_aiocbs_running - 1) memcpy(temp_running_ops + j, running_ops + j + 1, (num_aiocbs_running - 1 - j) * sizeof(PVFS_sys_op_id));
               memcpy(running_ops, temp_running_ops, (PVFS_AIO_MAX_RUNNING * sizeof(PVFS_sys_op_id)));
               break;
            }
         }

         num_aiocbs_running--;
         gen_mutex_lock(&waiting_list_mutex);
         if (!num_aiocbs_running && qlist_empty(aio_waiting_list))
         {
            gossip_debug(GOSSIP_USRINT_DEBUG, "No running requests, progress thread exiting\n");
            progress_running = PVFS_AIO_PROGRESS_IDLE;
            gen_mutex_unlock(&waiting_list_mutex);
            gen_mutex_unlock(&progress_sync_mutex);
            pthread_exit(NULL);
         }
         gen_mutex_unlock(&waiting_list_mutex);
      }
      gen_mutex_unlock(&progress_sync_mutex);
   }
}

/* this function is called to remove finished cbs after calling aio_return() */
void aiocommon_remove_cb(struct pvfs_aiocb *p_cb)
{
   gen_mutex_lock(&finished_list_mutex);
   qlist_del(&(p_cb->link));
   gen_mutex_unlock(&finished_list_mutex);

   /* free the mem and file requests */
   PVFS_Request_free(&(p_cb->mem_req));
   PVFS_Request_free(&(p_cb->file_req));
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
