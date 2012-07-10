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

#define AIO_SET_ERR(rc)                             \
do {                                                \
    if (IS_PVFS_NON_ERRNO_ERROR(-(rc)))             \
    {                                               \
        pvfs_errno = -rc;                           \
        errno = EIO;                                \
    }                                               \
    else if (IS_PVFS_ERROR(-(rc)))                  \
    {                                               \
        errno = PINT_errno_mapping[(-(rc)) & 0x7f]; \
    }                                               \
} while (0)         

typedef enum
{
    PVFS_AIO_IO_OP = 1,
    PVFS_AIO_OPEN_OP,
} PVFS_aio_op_code;

/*struct pvfs_aiocb
{
    PVFS_sys_op_id op_id;
    PVFS_sysresp_io io_resp;
    PVFS_Request mem_req;
    PVFS_Request file_req;

    struct aiocb *a_cb;
    struct qlist_head link;
};*/

struct PINT_aio_io_cb
{

}

struct PINT_aio_open_cb
{
    char *path;     /* in */
    int flags;      /* in */
    PVFS_hint file_creation_param;  /* in */
    int mode;       /* in */
    int *fd;                /* in/out */
    pvfs_descriptor *pd;    /* in/out */
}

/* a pvfs async control block, used for keeping track of async
 * operations outstanding in the filesystem
 */
struct pvfs_aiocb
{
    PVFS_sys_op_id op_id; 
    PVFS_credential *cred_p;
    PVFS_hint hints;

    PVFS_aio_op_code op_code;
    PVFS_error error_code;
    struct qlist_head link;

    struct aiocb *a_cb; /* acb if present ???? */

    int (*call_back_fn)(void *c_dat, int status);
    void *c_dat;

    union
    {
        struct PINT_aio_io_cb io;
        struct PINT_aio_open_cb open;
    } u;
}


int aiocommon_init(void);

int aiocommon_lio_listio(struct pvfs_aiocb *list[], int nent);

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
