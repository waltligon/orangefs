/*
 * (C) 2002 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __DBPF_BSTREAM_H__
#define __DBPF_BSTREAM_H__

#if defined(__cplusplus)
extern "C"
{
#endif

#ifdef __PVFS2_USE_AIO__
#include <aio.h>

#else
    int dbpf_bstream_threaded_initalize(
    void);
    int dbpf_bstream_threaded_finalize(
    void);
    int dbpf_bstream_threaded_set_thread_count(
    int count);

    enum IO_queue_type
    {
        IO_QUEUE_RESIZE,
        IO_QUEUE_WRITE,
        IO_QUEUE_READ,
        IO_QUEUE_FLUSH,
        IO_QUEUE_LAST
    };
#endif

#include "trove.h"
#include "dbpf.h"


#if defined(__cplusplus)
}
#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
