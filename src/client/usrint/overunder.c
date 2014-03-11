/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - implementation of stdio for pvfs
 */

#define USRINT_SOURCE 1
#include "usrint.h"
#include "stdio-ops.h"
#include "openfile-util.h"

int __underflow (FILE *stream)
{
    int bytes_read = 0;
    /* check for vtable and wide char support */
    if (!stream || !ISMAGICSET(stream, _P_IO_MAGIC))
    {
        if (stream && ISMAGICSET(stream, _IO_MAGIC))
        {
            /* return glibc_ops.underflow */
        }
        errno = EINVAL;
        return -1;
    }
    if (ISFLAGSET(stream, _IO_CURRENTLY_PUTTING))
    {
        pvfs_set_to_get(stream);
    }
    /* if buffer not empty, return next char */
    if (stream->_IO_read_ptr < stream->_IO_read_end)
    {
        return *(unsigned char *) stream->_IO_read_ptr;
    }
    /* check for backup */
    /* check for markers */
    bytes_read = pvfs_read_buf(stream);
    if (bytes_read > 0)
    {
        return *(unsigned char *) stream->_IO_read_ptr;
    }
    else
    {
        return EOF;
    }
}

int __uflow (FILE *stream)
{
    int bytes_read = 0;
    /* check for vtable and wide char support */
    if (!stream || !ISMAGICSET(stream, _P_IO_MAGIC))
    {
        if (stream && ISMAGICSET(stream, _IO_MAGIC))
        {
            /* return glibc_ops.underflow */
        }
        errno = EINVAL;
        return -1;
    }
    if (ISFLAGSET(stream, _IO_CURRENTLY_PUTTING))
    {
        pvfs_set_to_get(stream);
    }
    /* if buffer not empty, return next char */
    if (stream->_IO_read_ptr < stream->_IO_read_end)
    {
        return *(unsigned char *) stream->_IO_read_ptr++;
    }
    /* check for backup */
    /* check for markers */
    bytes_read = pvfs_read_buf(stream);
    if (bytes_read > 0)
    {
        return *(unsigned char *) stream->_IO_read_ptr++;
    }
    else
    {
        return EOF;
    }
}

int __overflow (FILE *stream, int ch)
{
    int rc = 0;
    /* This is a single-byte stream.  */
    /* check for wide mode */
    if (!stream || !ISMAGICSET(stream, _P_IO_MAGIC))
    {
        if (stream && ISMAGICSET(stream, _IO_MAGIC))
        {
            /* return glibc_ops.underflow */
        }
        errno = EINVAL;
        return -1;
    }
    if (!ISFLAGSET(stream, _IO_CURRENTLY_PUTTING))
    {
        pvfs_set_to_put(stream);
    }
    else
    {
        rc = pvfs_write_buf(stream);
    }
    if (rc == -1)
    {
        return EOF;
    }
    *(unsigned char *) stream->_IO_write_ptr++ = ch;
    return ch;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
