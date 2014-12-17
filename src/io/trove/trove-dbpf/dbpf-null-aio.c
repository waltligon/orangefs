
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include <assert.h>
#include <errno.h>
#include <aio.h>

#include "gossip.h"
#include "pvfs2-debug.h"
#include "trove.h"
#include "trove-internal.h"
#include "dbpf.h"
#include "quicklist.h"
#include "pthread.h"
#include "dbpf.h"


static int null_lio_listio(int mode, struct aiocb * const list[],
                          int nent, struct sigevent *sig);
static int null_aio_error(const struct aiocb *aiocbp);
static ssize_t null_aio_return(struct aiocb *aiocbp);
static int null_aio_cancel(int filedesc, struct aiocb * aiocbp);
static int null_aio_suspend(const struct aiocb * const list[], int nent,
                           const struct timespec * timeout);
static int null_aio_read(struct aiocb * aiocbp);
static int null_aio_write(struct aiocb * aiocbp);
static int null_aio_fsync(int operation, struct aiocb * aiocbp);

static struct dbpf_aio_ops null_aio_ops;

struct null_aio_item
{
    struct aiocb *cb_p;
    struct sigevent *sig;
    struct qlist_head list_link;
    int master;
    pthread_t *tids;
    int nent;
};
static void* null_lio_thread(void*);

int null_lio_listio(int mode, struct aiocb * const list[], 
                   int nent, struct sigevent *sig) 
{
    struct null_aio_item* tmp_item;
    int ret, i;
    pthread_t *tids;
    pthread_attr_t attr;

    tids = (pthread_t *)malloc(sizeof(pthread_t) * nent);
    if(!tids)
    {
        return (-1);
    }

    for(i = 0; i < nent; ++i)
    {
	int spawnmode= PTHREAD_CREATE_JOINABLE;
        tmp_item = (struct null_aio_item*)malloc(sizeof(struct null_aio_item)*nent);
        if(!tmp_item)
        {
            return (-1);
        }
        memset(tmp_item, 0, sizeof(struct null_aio_item));

        if(mode == LIO_NOWAIT && i == (nent - 1))
        {
            /* This is the master thread and needs to wait for the others.
             * We make the master the last thread to get created, so that
             * we don't end up in a race with the thread ids getting set
             * properly 
             */
            tmp_item->master = 1;
            tmp_item->tids = tids;
            tmp_item->nent = nent;
	    spawnmode= PTHREAD_CREATE_DETACHED;
        }

        tmp_item->cb_p = list[i];
        tmp_item->sig = sig;

        /* setup state */
#ifdef HAVE_AIOCB_ERROR_CODE
        tmp_item->cb_p->__error_code = EINPROGRESS;
#endif

        /* set detached state */
        ret = pthread_attr_init(&attr);
        if(ret != 0)
        {
            free(tmp_item);
            errno = ret;

            return(-1);
        }
        ret = pthread_attr_setdetachstate(
            &attr,
	    spawnmode
        );
        if(ret != 0)
        {
            free(tmp_item);
            errno = ret;
            return(-1);
        }

        /* create thread to perform I/O and trigger callback */
        ret = pthread_create(&tids[i], &attr, null_lio_thread, tmp_item);
        if(ret != 0)
        {
            int j = 0;

            if(mode == LIO_WAIT)
            {
                for(; j < i; ++j)
                {
                    pthread_join(tids[j], NULL);
                }
            }

            free(tmp_item);
            free(tids);
            errno = ret;
            return(-1);
        }
        gossip_debug(GOSSIP_BSTREAM_DEBUG, 
                     "[null-aio]: pthread_create completed:"
                     " id: %d, thread_id: %p\n",
                     i, (void *)tids[i]);
    }

    ret = 0;
    if(mode == LIO_WAIT)
    {
        for(i = 0; i < nent; ++i)
        {
            pthread_join(tids[i], NULL);
            if(ret != 0 && null_aio_error(list[i]) != 0)
            {
                /* for now we're just overwriting previous errors
                 * since we have no way to store and return them
                 * in the blocking case.
                 * The caller should call aio_error to get the
                 * element specific errors
                 */
                ret = null_aio_error(list[i]);
            }
        }

        free(tids);
    }
    return(ret);
}

static int null_aio_error(const struct aiocb *aiocbp)
{
#ifdef HAVE_AIOCB_ERROR_CODE
    return aiocbp->__error_code;
#else
    return 0;
#endif
}

static ssize_t null_aio_return(struct aiocb *aiocbp)
{
#ifdef HAVE_AIOCB_RETURN_VALUE
    return aiocbp->__return_value;
#else
    return 0;
#endif
}

static int null_aio_cancel(int filedesc, struct aiocb *aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static int null_aio_suspend(const struct aiocb * const list[], int nent,
                           const struct timespec * timeout)
{
    errno = ENOSYS;
    return -1;
}

static int null_aio_read(struct aiocb * aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static int null_aio_write(struct aiocb * aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static int null_aio_fsync(int operation, struct aiocb * aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static void* null_lio_thread(void* foo)
{
    struct null_aio_item* tmp_item = (struct null_aio_item*)foo;
    int ret = 0;
    struct stat statbuf;

    if(tmp_item->cb_p->aio_lio_opcode == LIO_READ)
    {
        ret = tmp_item->cb_p->aio_nbytes;
    }
    else if(tmp_item->cb_p->aio_lio_opcode == LIO_WRITE)
    {
        gossip_debug(GOSSIP_BSTREAM_DEBUG,
                     "[null-aio]: pwrite: cb_p: %p, "
                     "fd: %d, bufp: %p, size: %zd off:%llu\n",
                     tmp_item->cb_p, tmp_item->cb_p->aio_fildes, 
                     tmp_item->cb_p->aio_buf, tmp_item->cb_p->aio_nbytes,
		     llu(tmp_item->cb_p->aio_offset));

        /* check size of file */
        /* note, if either fstat or ftruncate fail, then we let the ret and
         * errno drop through to the logic below.  Otherwise we report the
         * size that would have been written.
         */
        ret = fstat(tmp_item->cb_p->aio_fildes, &statbuf);
        if(ret == 0)
        {
            if(statbuf.st_size < 
                (tmp_item->cb_p->aio_nbytes + tmp_item->cb_p->aio_offset))
            {
                /* this write would extend the file */
                ret = ftruncate(tmp_item->cb_p->aio_fildes, 
                    (tmp_item->cb_p->aio_nbytes + tmp_item->cb_p->aio_offset));
                if(ret == 0)
                {
                    ret = tmp_item->cb_p->aio_nbytes;
                }
            }
            else
            {
                ret = tmp_item->cb_p->aio_nbytes;
            }
        }
    }
    else
    {
        /* this should have been caught already */
        assert(0);
    }

    /* store error and return codes */
    if(ret < 0)
    {
#ifdef HAVE_AIOCB_ERROR_CODE
        tmp_item->cb_p->__error_code = errno;
#endif
    }
    else
    {
#ifdef HAVE_AIOCB_ERROR_CODE
        tmp_item->cb_p->__error_code = 0;
#endif

#ifdef HAVE_AIOCB_RETURN_VALUE
        tmp_item->cb_p->__return_value = ret;
#endif
    }

    if(tmp_item->master)
    {
        int i;
        /* I'm the master, gotta wait for the others to call notify */

        /* we skip the last one because that's us */
        for(i = 0; i < (tmp_item->nent - 1); ++i)
        {
            ret = pthread_join(tmp_item->tids[i], NULL);
            if(ret != 0)
            {
                gossip_err("pthread_join failed: %d (%s), i: %d, tid: %p\n",
                           ret, strerror(ret), i, (void *)tmp_item->tids[i]);
            }
        }

        free(tmp_item->tids);
        /* run callback fn */
        tmp_item->sig->sigev_notify_function(tmp_item->sig->sigev_value);
    }

    free(tmp_item);

    pthread_exit(NULL);
    return NULL;
}

static int null_aio_bstream_read_list(TROVE_coll_id coll_id,
                                     TROVE_handle handle,
                                     char **mem_offset_array, 
                                     TROVE_size *mem_size_array,
                                     int mem_count,
                                     TROVE_offset *stream_offset_array, 
                                     TROVE_size *stream_size_array,
                                     int stream_count,
                                     TROVE_size *out_size_p,
                                     TROVE_ds_flags flags, 
                                     TROVE_vtag_s *vtag,
                                     void *user_ptr,
                                     TROVE_context_id context_id,
                                     TROVE_op_id *out_op_id_p,
                                     PVFS_hint hints)
{
    return dbpf_bstream_rw_list(coll_id,
                                handle,
                                mem_offset_array, 
                                mem_size_array,
                                mem_count,
                                stream_offset_array, 
                                stream_size_array,
                                stream_count,
                                out_size_p,
                                flags, 
                                vtag,
                                user_ptr,
                                context_id,
                                out_op_id_p,
                                LIO_READ,
                                &null_aio_ops,
                                hints);
}

static int null_aio_bstream_write_list(TROVE_coll_id coll_id,
                                      TROVE_handle handle,
                                      char **mem_offset_array, 
                                      TROVE_size *mem_size_array,
                                      int mem_count,
                                      TROVE_offset *stream_offset_array, 
                                      TROVE_size *stream_size_array,
                                      int stream_count,
                                      TROVE_size *out_size_p,
                                      TROVE_ds_flags flags, 
                                      TROVE_vtag_s *vtag,
                                      void *user_ptr,
                                      TROVE_context_id context_id,
                                      TROVE_op_id *out_op_id_p,
                                      PVFS_hint hints)
{
    return dbpf_bstream_rw_list(coll_id,
                                handle,
                                mem_offset_array, 
                                mem_size_array,
                                mem_count,
                                stream_offset_array, 
                                stream_size_array,
                                stream_count,
                                out_size_p,
                                flags, 
                                vtag,
                                user_ptr,
                                context_id,
                                out_op_id_p,
                                LIO_WRITE,
                                &null_aio_ops,
                                hints);
}

static struct dbpf_aio_ops null_aio_ops =
{
    null_aio_read,
    null_aio_write,
    null_lio_listio,
    null_aio_error,
    null_aio_return,
    null_aio_cancel,
    null_aio_suspend,
    null_aio_fsync
};

struct TROVE_bstream_ops null_aio_bstream_ops =
{
    dbpf_bstream_read_at,
    dbpf_bstream_write_at,
    dbpf_bstream_resize,
    dbpf_bstream_validate,
    null_aio_bstream_read_list,
    null_aio_bstream_write_list,
    dbpf_bstream_flush,
    NULL
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
