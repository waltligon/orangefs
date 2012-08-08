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
    PVFS_AIO_IOV_OP,
    PVFS_AIO_OPEN_OP,
    PVFS_AIO_RENAME_OP,
    PVFS_AIO_TRUNC_OP,
    PVFS_AIO_CLOSE_OP,
    PVFS_AIO_STAT_OP,
    PVFS_AIO_STAT64_OP,
    PVFS_AIO_CHOWN_OP,
    PVFS_AIO_CHMOD_OP,
    PVFS_AIO_MKDIR_OP,
    PVFS_AIO_REMOVE_OP,
} PVFS_aio_op_code;

/* the following structures contain operation dependent data for aio calls */
struct PINT_aio_io_cb
{
    struct iovec *vector;       /* in */
    int count;                  /* in (readv writev )*/
    pvfs_descriptor *pd;        /* in */
    enum PVFS_io_type which;    /* in */
    off64_t offset;             /* in */
    int advance_fp;             /* in */
    void *sys_buf;              
    PVFS_Request mem_req;       
    PVFS_Request file_req;      
    PVFS_sysresp_io io_resp;    
    ssize_t *bcnt;              /* out */
};

struct PINT_aio_open_cb
{
    char *path;                     /* in */
    char *directory;                /* in */
    char *filename;                 /* in */
    int flags;                      /* in */
    PVFS_hint file_creation_param;  /* in */
    int mode;                       /* in */
    pvfs_descriptor *pdir;          /* in */
    int *fd;                        /* in/out */
    pvfs_descriptor *pd;            /* out */
};

struct PINT_aio_rename_cb
{
    PVFS_object_ref *oldpdir;   /* in */
    char *olddir;               /* in */
    char *oldname;              /* in */
    PVFS_object_ref *newpdir;   /* in */
    char *newdir;               /* in */
    char *newname;              /* in */
};

struct PINT_aio_trunc_cb
{
    pvfs_descriptor *pd;    /* in */
    off64_t length;         /* in */
};

struct PINT_aio_stat_cb
{
    pvfs_descriptor *pd;        /* in */
    uint32_t mask;              /* in */
    PVFS_sysresp_getattr getattr_resp;
    void  *buf;           /* out */
};

struct PINT_aio_chown_cb
{
    pvfs_descriptor *pd;    /* in */
    PVFS_sys_attr attr;     /* in */
};

struct PINT_aio_chmod_cb
{
    pvfs_descriptor *pd;    /* in */
    PVFS_sys_attr attr;     /* in */
};

struct PINT_aio_mkdir_cb
{
    char *directory;        /* in */
    char *filename;         /* in */
    PVFS_object_ref *pdir;  /* in */
    mode_t mode;            /* in */
};

struct PINT_aio_remove_cb
{
    char *directory;        /* in */
    char *filename;         /* in */
    PVFS_object_ref *pdir;  /* in */
    int dirflag;            /* in */
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
        struct PINT_aio_rename_cb rename;
        struct PINT_aio_trunc_cb trunc;
        struct PINT_aio_stat_cb stat;
        struct PINT_aio_chown_cb chown;
        struct PINT_aio_chmod_cb chmod;
        struct PINT_aio_mkdir_cb mkdir;
        struct PINT_aio_remove_cb remove;
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
