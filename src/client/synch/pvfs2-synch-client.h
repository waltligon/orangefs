/*
 * (C) 2006 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 *
 */
#ifndef _PVFS2_SYNCH_CLIENT_H
#define _PVFS2_SYNCH_CLIENT_H

#include "pvfs2.h"

extern PVFS_error PINT_synch_init(void);
extern void PINT_synch_cleanup(void);
/*
 * if timeout < 0  ==> post a synchronization operation that will prevent this 
 *                     sm progress but allows other sm's to progress.
 * if timeout == 0 ==> invoke a infinite blocking synchronization operation that 
 *                     will prevent any sm from progressing.
 * if timeout > 0  ==> invoke a finite blocking synchronization operation that will
 *                     prevent any sm from progressing upto timeout mseconds.
 */
extern PVFS_error PINT_pre_synch(enum PVFS_synch_method method,
                                PVFS_object_ref *ref, 
                                enum PVFS_io_type io_type,
                                PVFS_offset  offset,
                                PVFS_size  count,
                                int    stripe_size,
                                int    nservers,
                                int    timeout,
                                void **user_ptr);
extern PVFS_error PINT_post_synch(void *user_ptr, int timeout);
extern PVFS_error PINT_synch_wait(void *user_ptr);
extern int PINT_synch_ping(PVFS_fs_id fsid, const char *, enum PVFS_synch_method method);

struct synch_dlm_result {
    int64_t dlm_token;
};

struct synch_vec_result {
    int     nvector;
    unsigned int     *vector;
};

struct synch_result {
    enum PVFS_synch_method method;
    union {
        struct synch_dlm_result dlm;
        struct synch_vec_result vec;
    } synch;
};

extern struct synch_result *PINT_synch_result(void *user_ptr);
extern void PINT_synch_result_dtor(struct synch_result *);
PVFS_error PINT_synch_cancel(void **user_ptr);

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
