/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __REQUEST_SCHEDULER_H
#define __REQUEST_SCHEDULER_H

#include "pvfs2-req-proto.h"

/* this file contains the API for doing server side scheduling at
 * the request level.  
 */

typedef PVFS_id_gen_t req_sched_id;
typedef int req_sched_error_code;

/* setup and teardown */

int PINT_req_sched_initialize(
    void);

int PINT_req_sched_finalize(
    void);

/* retrieving information about incoming requests */

int PINT_req_sched_target_handle(
    struct PVFS_server_req *req,
    int req_index,
    PVFS_handle * handle,
    PVFS_fs_id * fs_id,
    int* readonly_flag);

/* scheduler submission */

int PINT_req_sched_post(
    struct PVFS_server_req *in_request,
    int req_index,
    void *in_user_ptr,
    req_sched_id * out_id);

int PINT_req_sched_unpost(
    req_sched_id in_id,
    void **returned_user_ptr);

int PINT_req_sched_release(
    req_sched_id in_completed_id,
    void *in_user_ptr,
    req_sched_id * out_id);

int PINT_req_sched_post_timer(
    int msecs,
    void *in_user_ptr,
    req_sched_id * out_id);

/* testing for completion */

int PINT_req_sched_test(
    req_sched_id in_id,
    int *out_count_p,
    void **returned_user_ptr_p,
    req_sched_error_code * out_status);

int PINT_req_sched_testsome(
    req_sched_id * in_id_array,
    int *inout_count_p,
    int *out_index_array,
    void **returned_user_ptr_array,
    req_sched_error_code * out_status_array);

int PINT_req_sched_testworld(
    int *inout_count_p,
    req_sched_id * out_id_array,
    void **returned_user_ptr_array,
    req_sched_error_code * out_status_array);

enum PVFS_server_mode PINT_req_sched_get_mode(void);

#endif /* __REQUEST_SCHEDULER_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
