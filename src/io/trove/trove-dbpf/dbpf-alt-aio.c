
#include "quicklist.h"
#include "dbpf-alt-aio.h"
#include "pthread.h"
#include "dbpf.h"

static int alt_lio_listio(int mode, struct aiocb * const list[],
                          int nent, struct sigevent *sig);
static int alt_aio_error(const struct aiocb *aiocbp);
static ssize_t alt_aio_return(struct aiocb *aiocbp);
static int alt_aio_cancel(int filedesc, struct aiocb * aiocbp);
static int alt_aio_suspend(const struct aiocb * const list[], int nent,
                           const struct timespec * timeout);
static int alt_aio_read(struct aiocb * aiocbp);
static int alt_aio_write(struct aiocb * aiocbp);
static int alt_aio_fsync(int operation, struct aiocb * aiocbp);

static struct dbpf_aio_ops alt_aio_ops;

struct alt_aio_item
{   
    struct aiocb *cb_p;
    struct sigevent *sig;
    struct qlist_head list_link;
};      
static void* alt_lio_thread(void*);

int alt_lio_listio(int mode, struct aiocb * const list[], 
		   int nent, struct sigevent *sig) 
{   
    struct alt_aio_item* tmp_item;
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
	tmp_item = (struct alt_aio_item*)malloc(sizeof(struct alt_aio_item)*nent);
	if(!tmp_item)
	{
	    return (-1);
	}

	tmp_item->cb_p = list[i];
	tmp_item->sig = sig;

	/* setup state */
	tmp_item->cb_p->__error_code = EINPROGRESS;

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
	    (mode == LIO_WAIT) ? 
	    PTHREAD_CREATE_JOINABLE :
	    PTHREAD_CREATE_DETACHED);
	if(ret != 0)
	{
	    free(tmp_item);
	    errno = ret;
	    return(-1);
	}

	/* create thread to perform I/O and trigger callback */
	ret = pthread_create(&tids[i], &attr, alt_lio_thread, tmp_item);
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
	    errno = ret;
	    return(-1);
	}
    }

    ret = 0;
    if(mode == LIO_WAIT)
    {
	for(i = 0; i < nent; ++i)
	{
	    pthread_join(tids[i], NULL);
	    if(ret != 0 && alt_aio_error(list[i]) != 0)
	    {
		ret = alt_aio_error(list[i]);
	    }
	}
    }

    free(tids);

    return(ret);
}

static int alt_aio_error(const struct aiocb *aiocbp)
{
    return aiocbp->__error_code;
}

static ssize_t alt_aio_return(struct aiocb *aiocbp)
{
    return aiocbp->__return_value;
}

static int alt_aio_cancel(int filedesc, struct aiocb *aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static int alt_aio_suspend(const struct aiocb * const list[], int nent,
                    const struct timespec * timeout)
{
    errno = ENOSYS;
    return -1;
}

static int alt_aio_read(struct aiocb * aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static int alt_aio_write(struct aiocb * aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static int alt_aio_fsync(int operation, struct aiocb * aiocbp)
{
    errno = ENOSYS;
    return -1;
}

static void* alt_lio_thread(void* foo)
{
    struct alt_aio_item* tmp_item = (struct alt_aio_item*)foo;
    int ret = 0;

    if(tmp_item->cb_p->aio_lio_opcode == LIO_READ)
    {
        ret = pread(tmp_item->cb_p->aio_fildes,
                    (void*)tmp_item->cb_p->aio_buf,
                    tmp_item->cb_p->aio_nbytes,
                    tmp_item->cb_p->aio_offset);
    }
    else if(tmp_item->cb_p->aio_lio_opcode == LIO_WRITE)
    {
        int i = 0;
        gossip_debug(GOSSIP_BSTREAM_DEBUG,
                     "[alt-aio]: pwrite: cb_p: %p, "
                     "fd: %d, bufp: %p, size: %d\n",
                     tmp_item->cb_p, tmp_item->cb_p->aio_fildes, 
                     tmp_item->cb_p->aio_buf, tmp_item->cb_p->aio_nbytes);

        ret = pwrite(tmp_item->cb_p->aio_fildes,
                     (const void*)tmp_item->cb_p->aio_buf,
                     tmp_item->cb_p->aio_nbytes,
                     tmp_item->cb_p->aio_offset);
    }
    else
    {
	/* this should have been caught already */
	assert(0);
    }

    /* store error and return codes */
    if(ret < 0)
    {
	tmp_item->cb_p->__error_code = errno;
    }
    else
    {
	tmp_item->cb_p->__error_code = 0;
	tmp_item->cb_p->__return_value = ret;
    }

    /* run callback fn */
    tmp_item->sig->sigev_notify_function(
	tmp_item->sig->sigev_value);

    free(tmp_item);

    return(NULL);
}

static int alt_aio_bstream_read_list(TROVE_coll_id coll_id,
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
                                     TROVE_op_id *out_op_id_p)
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
                                &alt_aio_ops);
}

static int alt_aio_bstream_write_list(TROVE_coll_id coll_id,
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
                                      TROVE_op_id *out_op_id_p)
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
                                &alt_aio_ops);
}

static struct dbpf_aio_ops alt_aio_ops =
{
    alt_aio_read,
    alt_aio_write,
    alt_lio_listio,
    alt_aio_error,
    alt_aio_return,
    alt_aio_cancel,
    alt_aio_suspend,
    alt_aio_fsync
};

struct TROVE_bstream_ops alt_aio_bstream_ops =
{
    dbpf_bstream_read_at,
    dbpf_bstream_write_at,
    dbpf_bstream_resize,
    dbpf_bstream_validate,
    alt_aio_bstream_read_list,
    alt_aio_bstream_write_list,
    dbpf_bstream_flush
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
