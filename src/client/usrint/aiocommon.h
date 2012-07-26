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
#include "pvfs2-aio.h"

#define PVFS_AIO_MAX_RUNNING 10
#define PVFS_AIO_LISTIO_MAX 10

#define PVFS_AIO_DEFAULT_TIMEOUT_MS 10

enum
{
    PVFS_AIO_PROGRESS_IDLE,
    PVFS_AIO_PROGRESS_RUNNING,
};

typedef enum
{
    PVFS_AIO_IO_OP = 1,
    PVFS_AIO_OPEN_OP,
} PVFS_aio_op_code;


/* the following structures contain operation dependent data for aio calls */
struct PINT_aio_io_cb
{
    struct iovec vector;        /* in */
    pvfs_descriptor *pd;        /* in */
    enum PVFS_io_type which;    /* in */
    off64_t offset;             /* in */
    void *sys_buf;              
    PVFS_Request mem_req;       
    PVFS_Request file_req;      
    PVFS_sysresp_io io_resp;    
    ssize_t *bcnt;              /* out */
};

struct PINT_aio_open_cb
{
    const char *path;               /* in */
    int flags;                      /* in */
    PVFS_hint file_creation_param;  /* in */
    int mode;                       /* in */
    pvfs_descriptor *pdir;          /* in */
    int *fd;                        /* in/out */
    pvfs_descriptor *pd;            /* out */
};

/* a pvfs async control block, used for keeping track of async
 * operations outstanding in the filesystem
 */
struct pvfs_aiocb
{
    PVFS_sys_op_id op_id; 
    PVFS_hint hints;

    PVFS_aio_op_code op_code;
    PVFS_error error_code;
    struct qlist_head link;

    int (*call_back_fn)(void *c_dat, int status);
    void *call_back_dat;

    union
    {
        struct PINT_aio_io_cb io;
        struct PINT_aio_open_cb open;
    } u;
};

int aiocommon_init(void);

void aiocommon_submit_op(struct pvfs_aiocb *p_cb);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

#endif
