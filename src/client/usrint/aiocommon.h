/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */
#ifndef AIOCOMMON_H
#define AIOCOMMON_H

#include <unistd.h>
#include <pthread.h>
#include "pvfs2-types.h"
#include "usrint.h"
#include "posix-ops.h"
#include "openfile-util.h"
#include "iocommon.h"
#include "aio.h"
#include "quicklist.h"
#include "gossip.h"

#define PVFS_AIO_MAX_RUNNING 10
#define PVFS_AIO_LISTIO_MAX 10

#define PVFS_AIO_PROGRESS_IDLE 0
#define PVFS_AIO_PROGRESS_RUNNING 1

#define PVFS_AIO_DEFAULT_TIMEOUT_MS 10

struct pvfs_aiocb
{
    PVFS_sys_op_id op_id;
    PVFS_sysresp_io io_resp;
    PVFS_Request mem_req;
    PVFS_Request file_req;

    struct aiocb *a_cb;
    struct qlist_head link;
};

int aiocommon_init(void);

int aiocommon_lio_listio(struct pvfs_aiocb *list[],
                         int nent);

void aiocommon_remove_cb(struct pvfs_aiocb *p_cb);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
