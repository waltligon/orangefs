/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "pvfs2-types.h"

/* PVFS_perror()
 *
 * prints a message on stderr, consisting of text argument
 * followed by a colon, space, and error string for the given
 * retcode.
 * NOTE: also prints a warning if the error code is not in pvfs2
 * format and assumes errno
 *
 * no return value
 */
void PVFS_perror(char* text, int retcode)
{
    if(IS_PVFS_ERROR(-retcode))
    {
	fprintf(stderr, "%s: %s\n", text,
	strerror(PVFS_ERROR_TO_ERRNO(-retcode)));
	/* TODO: probably we should do something to print
	 * out the class too?
	 */
    }       
    else
    {
	fprintf(stderr, "Warning: non PVFS2 error code:\n");
	fprintf(stderr, "%s: %s\n", text,
	strerror(-retcode));
    }

    return;
}

/* NOTE: PVFS error values are defined in include/pvfs2-types.h
 */

int32_t PINT_errno_mapping[PVFS_ERRNO_MAX + 1] =
{
    0,     /* leave this one empty */
    EPERM, /* 1 */
    ENOENT,
    EINTR,
    EIO,
    ENXIO,
    EBADF,
    EAGAIN,
    ENOMEM,
    EFAULT,
    EBUSY, /* 10 */
    EEXIST,
    ENODEV,
    ENOTDIR,
    EISDIR,
    EINVAL,
    EMFILE,
    EFBIG,
    ENOSPC,
    EROFS,
    EMLINK, /* 20 */
    EPIPE,
    EDEADLK,
    ENAMETOOLONG,
    ENOLCK,
    ENOSYS,
    ENOTEMPTY,
    ELOOP,
    EWOULDBLOCK,
    ENOMSG,
    EUNATCH, /* 30 */
    EBADR,
    EDEADLOCK,
    ENODATA,
    ETIME,
    ENONET,
    EREMOTE,
    ECOMM,
    EPROTO,
    EBADMSG,
    EOVERFLOW, /* 40 */
    ERESTART,
    EMSGSIZE,
    EPROTOTYPE,
    ENOPROTOOPT,
    EPROTONOSUPPORT,
    EOPNOTSUPP,
    EADDRINUSE,
    EADDRNOTAVAIL,
    ENETDOWN,
    ENETUNREACH, /* 50 */
    ENETRESET,
    ENOBUFS,
    ETIMEDOUT,
    ECONNREFUSED,
    EHOSTDOWN,
    EHOSTUNREACH,
    EALREADY   /* 57 */
};

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
