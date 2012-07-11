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
static void aiocommon_run_waiting_ops(void);
static void aiocommon_run_op(struct pvfs_aiocb *p_cb);
static void aiocommon_finish_op(struct pvfs_aiocb *p_cb);
static void *aiocommon_progress(void *ptr);

/* linked list variables used to implement aio structures */
static struct qlist_head *aio_waiting_list = NULL;
static struct qlist_head *aio_running_list = NULL;
static gen_mutex_t aio_wait_list_mutex = GEN_MUTEX_INITIALIZER;

/* PROGRESS THREAD VARIABLES */
static pthread_t aio_progress_thread;
static int aio_progress_status = PVFS_AIO_PROGRESS_IDLE;
static int aio_num_ops_running = 0;
static PVFS_sys_op_id aio_running_ops[PVFS_AIO_MAX_RUNNING] = {0};

/* Initialization of PVFS AIO system */
int aiocommon_init(void)
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

   gossip_debug(GOSSIP_USRINT_DEBUG, "Successfully initalized PVFS AIO inteface\n");

   return 0;
}

void aiocommon_submit_op(struct pvfs_aiocb *p_cb)
{
    p_cb->error_code = EINPROGRESS;
    gen_mutex_lock(&aio_wait_list_mutex);
    qlist_add_tail(&(p_cb->link), aio_waiting_list);
    if (aio_progress_status == PVFS_AIO_PROGRESS_IDLE)
    {
        pthread_create(&aio_progress_thread, NULL, aiocommon_progress, NULL);
        aio_progress_status = PVFS_AIO_PROGRESS_RUNNING;
    }
    gen_mutex_unlock(&aio_wait_list_mutex);

    return;
}

static void aiocommon_run_waiting_ops(void)
{
    struct qlist_head *next_op;
    struct pvfs_aiocb *p_cb;

    while(1)
    {
        gen_mutex_lock(&aio_wait_list_mutex);
        if (aio_num_ops_running == PVFS_AIO_MAX_RUNNING ||
            qlist_empty(aio_waiting_list))
        {
            break;
        }

        next_op = qlist_pop(aio_waiting_list);
        p_cb = qlist_entry(next_op, struct pvfs_aiocb, link);
        gossip_debug(GOSSIP_USRINT_DEBUG, "Adding AIO CB %p to running list\n",
                     p_cb);

        aiocommon_run_op(p_cb);
        gen_mutex_unlock(&aio_wait_list_mutex);
    }

    return;
}

static void aiocommon_run_op(struct pvfs_aiocb *p_cb)
{
    int rc = 0;

    switch(p_cb->op_code)
    {
        case PVFS_AIO_IO_OP:
            break;
        case PVFS_AIO_OPEN_OP:
            rc = PVFS_iaio_open(&p_cb->u.open.pd,
                                p_cb->u.open.path,
                                p_cb->u.open.flags, 
                                p_cb->u.open.file_creation_param,
                                p_cb->u.open.mode,
                                NULL,
                                p_cb->cred_p,
                                &p_cb->op_id,
                                p_cb->hints,
                                (void *)p_cb);
            break;
        default:
            rc = -PVFS_EINVAL;
            break;
    }

    if (rc < 0 || (rc == 0 && p_cb->op_id == -1))
    {
        p_cb->error_code = rc;
        aiocommon_finish_op(p_cb);
    }
    else
    {
        /* the operation deferred completion */
        gossip_debug(GOSSIP_USRINT_DEBUG, "AIO CB %p, DEFERRED\n", p_cb);
        qlist_add_tail(&p_cb->link, aio_running_list);
        aio_running_ops[aio_num_ops_running++] = p_cb->op_id;
    }

    return;
}

static void aiocommon_finish_op(struct pvfs_aiocb *p_cb)
{
    int status = 0;

    switch (p_cb->op_code)
    {
        case PVFS_AIO_IO_OP:
            break;
        case PVFS_AIO_OPEN_OP:
            if (p_cb->error_code < 0)
            {
                *(p_cb->u.open.fd) = -1;
            }
            else
            {
                *(p_cb->u.open.fd) = p_cb->u.open.pd->fd;
            }
            break;
        default:
            break;
    }

    /* set the status with the error number, if an error occured */
    if (p_cb->error_code < 0)
    {
        if (IS_PVFS_NON_ERRNO_ERROR(-(p_cb->error_code)))
        {
            status = EIO;
        }
        else if (IS_PVFS_ERROR(-(p_cb->error_code)))
        {
            status = PINT_errno_mapping[(-(p_cb->error_code)) & 0x7f];
        }
    }

    if (p_cb->call_back_fn)
    {
        (*p_cb->call_back_fn)(p_cb->call_back_dat, status);
    }

    /* TODO: free stuff */
    free(p_cb);

    return;
}

static void *aiocommon_progress(void *ptr)
{
    int i, j;
    int ret = 0;
    int op_count = 0;
    PVFS_sys_op_id ret_op_ids[PVFS_AIO_MAX_RUNNING];
    PVFS_sys_op_id temp_running_ops[PVFS_AIO_MAX_RUNNING] = {0};
    int err_code_array[PVFS_AIO_MAX_RUNNING] = {0};
    struct pvfs_aiocb *pcb_array[PVFS_AIO_MAX_RUNNING] = {NULL};

    gossip_debug(GOSSIP_USRINT_DEBUG, "AIO progress thread starting up\n");

    /* progress thread */
    while (1)
    {
        aiocommon_run_waiting_ops();      

        /* call PVFS_sys_testsome() to force progress on "running" operations
         * in the system.
         * NOTE: the op_ids of the completed ops will be in ret_op_ids,
         * the number of operations will be in op_count, the user pointers
         * are stored in pcb_array, and the error codes are stored in
         * err_code array.
         */
        memcpy(ret_op_ids, aio_running_ops,
               (aio_num_ops_running * sizeof(PVFS_sys_op_id)));
        op_count = aio_num_ops_running;
        ret = PVFS_sys_testsome(ret_op_ids,
                                &op_count,
                                (void *)pcb_array,
                                err_code_array,
                                PVFS_AIO_DEFAULT_TIMEOUT_MS);

        /* for each op returned */
        for (i = 0; i < op_count; i++)
        {
            /* ignore completed items that do not have a user pointer
             * (these are not aio control blocks)
             */
            if (pcb_array[i] == NULL) continue;

            /* remove the cb from the running list */
            qlist_del(&(pcb_array[i]->link));

            pcb_array[i]->error_code = err_code_array[i];
            aiocommon_finish_op(pcb_array[i]);

            /* update the number of running cbs, and exit the thread
             * if this number is 0
             */
            for (j = 0; j < aio_num_ops_running; j++)
            {
                if (aio_running_ops[j] == pcb_array[i]->op_id)
                {
                    /* remove it from the running op_ids array */
                    if (j > 0)
                        memcpy(temp_running_ops, aio_running_ops,
                               j * sizeof(PVFS_sys_op_id));
                    if (j < aio_num_ops_running - 1)
                        memcpy(temp_running_ops + j, aio_running_ops + j + 1,
                               (aio_num_ops_running - 1 - j) * sizeof(PVFS_sys_op_id));
                    memcpy(aio_running_ops, temp_running_ops,
                           (PVFS_AIO_MAX_RUNNING * sizeof(PVFS_sys_op_id)));
                    break;
                }
            }

            aio_num_ops_running--;
            gen_mutex_lock(&aio_wait_list_mutex);
            if (!aio_num_ops_running && qlist_empty(aio_waiting_list))
            {
                gossip_debug(GOSSIP_USRINT_DEBUG,
                             "No AIO requests waiting, progress thread exiting\n");
                aio_progress_status = PVFS_AIO_PROGRESS_IDLE;
                gen_mutex_unlock(&aio_wait_list_mutex);
                pthread_exit(NULL);
            }
            gen_mutex_unlock(&aio_wait_list_mutex);
        }
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
