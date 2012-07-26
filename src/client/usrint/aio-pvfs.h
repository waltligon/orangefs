/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#ifndef AIO_PVFS_H
#define AIO_PVFS_H

#include "aio.h"

int pvfs_aio_cancel(int fd, struct aiocb *aiocbp);

int pvfs_aio_error(const struct aiocb *aiocbp);

//int pvfs_aio_fsync(int op, struct aiocb *aiocbp);

int pvfs_aio_read(struct aiocb *aiocbp);

ssize_t pvfs_aio_return(struct aiocb *aiocbp);

//int pvfs_aio_suspend(const struct aiocb * const cblist[], int n,
//		const struct timespec *timeout);

int pvfs_aio_write(struct aiocb *aiocbp);

int pvfs_lio_listio(int mode, struct aiocb * const list[], int nent,
		    struct sigevent *sig);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
